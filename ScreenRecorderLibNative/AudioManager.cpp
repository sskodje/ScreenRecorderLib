#include "AudioManager.h"
#include "cleanup.h"
#include <Functiondiscoverykeys_devpkey.h>
#include "CoreAudio.util.h"
#include <mutex>
using namespace std;

AudioManager::AudioManager() :
	m_AudioOptions(nullptr),
	m_IsCaptureEnabled(false),
	m_IsCapturePaused(false),
	m_AudioCaptures{},
	m_TimelineManager(nullptr)
{
	InitializeCriticalSection(&m_CriticalSection);
	m_OptionsListenerStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

AudioManager::~AudioManager()
{
	StopOptionsChangeListenerThread();
	CloseHandle(m_OptionsListenerStopEvent);
	DeleteCriticalSection(&m_CriticalSection);
	for each (WASAPICapture * capture in m_AudioCaptures) {
		delete capture;
	}
	m_AudioCaptures = {};
}

void AudioManager::OnOptionsChanged() {
	bool exit = false;
	const HANDLE events[] = {
	m_OptionsListenerStopEvent,
	m_AudioOptions->OnPropertyChangedEvent
	};
	while (!exit) {
		if (m_AudioOptions) {
			const DWORD ret = WaitForMultipleObjects(ARRAYSIZE(events), events,
								 false, INFINITE);
			switch (ret) {
				default:
				case WAIT_OBJECT_0: {
					exit = true;
					break;
				}
				case WAIT_OBJECT_0 + 1: {
					EnterCriticalSection(&m_CriticalSection);
					LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
					ConfigureAudioCapture(true);
					break;
				}
			}
		}
		else {
			const DWORD ret = WaitForSingleObject(m_OptionsListenerStopEvent, 100);
			if (ret == WAIT_OBJECT_0) {
				exit = true;
			}
		}
	}
}

HRESULT AudioManager::Initialize(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions, TimelineManager *pTimelineManager)
{
	HRESULT hr = S_OK;
	m_AudioOptions = audioOptions;
	m_TimelineManager = pTimelineManager;
	StopOptionsChangeListenerThread();
	ResetEvent(m_OptionsListenerStopEvent);
	hr = ConfigureAudioCapture(false);
	m_OptionsListenerThread = std::thread([this] {OnOptionsChanged(); });
	return hr;
}

bool const AudioManager::IsAnyAudioCapturesActive()
{
	for each (WASAPICapture * source in m_AudioCaptures)
	{
		if (IsAudioCaptureActive(source)) {
			return true;
		}
	}
	return false;
}

bool AudioManager::IsAudioCaptureActive(WASAPICapture *source)
{
	if (source->IsCapturing() && !source->IsPaused() && !source->IsSilent()) {
		return true;
	}
	return false;
}

void AudioManager::ClearRecordedBytes()
{
	for each (WASAPICapture * capture in m_AudioCaptures) {
		capture->ClearRecordedBytes();
	}
}

HRESULT AudioManager::StartCapture() {
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	m_IsCaptureEnabled = true;
	return ConfigureAudioCapture(true);
}

HRESULT AudioManager::StopCapture()
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	m_IsCaptureEnabled = false;
	return ConfigureAudioCapture(true);
}

HRESULT AudioManager::PauseCapture()
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	m_IsCapturePaused = true;
	return ConfigureAudioCapture(true);
}

HRESULT AudioManager::ResumeCapture()
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	m_IsCapturePaused = false;
	return ConfigureAudioCapture(true);
}

HRESULT AudioManager::StopOptionsChangeListenerThread()
{
	SetEvent(m_OptionsListenerStopEvent);
	try
	{
		if (m_OptionsListenerThread.joinable()) {
			m_OptionsListenerThread.join();
		}
		else {
			return S_FALSE;
		}
	}
	catch (...) {
		LOG_ERROR(L"Exception in StopOptionsChangeListenerThread");
		return E_FAIL;
	}
	return S_OK;
}

HRESULT AudioManager::StartDeviceCapture(WASAPICapture *pCapture) {
	HRESULT hr = pCapture->StartCapture();
	if (hr == S_OK) {
		LOG_INFO(L"Started audio capture on %s", pCapture->GetDeviceFriendlyName().c_str());
	}
	else if (hr == S_FALSE) {
		LOG_DEBUG(L"Audio capture on %s is already running", pCapture->GetDeviceFriendlyName().c_str());
	}
	return hr;
}

HRESULT AudioManager::StopDeviceCapture(WASAPICapture *pCapture) {
	if (pCapture->IsCapturing()) {
		RETURN_ON_BAD_HR(pCapture->StopCapture());
		LOG_DEBUG(L"Stopped audio capture on %s", pCapture->GetDeviceFriendlyName().c_str());
	}
	return S_FALSE;
}

HRESULT AudioManager::PauseDeviceCapture(WASAPICapture *pCapture)
{
	if (pCapture->IsCapturing() && !pCapture->IsPaused()) {
		RETURN_ON_BAD_HR(pCapture->PauseCapture());
		LOG_DEBUG(L"Paused audio capture on %s", pCapture->GetDeviceFriendlyName().c_str());
	}
	return S_FALSE;
}

HRESULT AudioManager::ResumeDeviceCapture(WASAPICapture *pCapture)
{
	if (pCapture->IsCapturing() && pCapture->IsPaused()) {
		RETURN_ON_BAD_HR(pCapture->ResumeCapture());
		LOG_DEBUG(L"Resumed audio capture on %s", pCapture->GetDeviceFriendlyName().c_str());
	}
	return S_FALSE;
}

HRESULT AudioManager::ConfigureAudioCapture(bool startDeviceCapture) {
	HRESULT hr = S_FALSE;
	MeasureExecutionTime measure(L"ConfigureAudioCapture");
	if (GetAudioOptions() == nullptr) {
		return hr;
	}
	std::vector<AUDIO_SOURCE *> &captureSources = GetAudioOptions()->GetAudioSources();

	for each (AUDIO_SOURCE * captureSource in captureSources)
	{
		WASAPICapture *wasapiCapture = nullptr;

		auto it = std::find_if(m_AudioCaptures.begin(), m_AudioCaptures.end(), [&](WASAPICapture *obj) {
			if (obj->GetAudioCaptureSource()->ID == captureSource->ID) {
				return true;
			}
			return false;
			});
		if (it == m_AudioCaptures.end()) {
			if (GetAudioOptions()->IsAudioEnabled()) {
				auto capture = new WASAPICapture(m_AudioOptions, captureSource);
				hr = capture->Initialize(captureSource->DeviceName, captureSource->Kind);
				if (SUCCEEDED(hr)) {
					m_AudioCaptures.insert(m_AudioCaptures.end(), capture);
					wasapiCapture = capture;
					LOG_DEBUG("Created WASAPI capture on %s with unique ID %s", wasapiCapture->GetDeviceFriendlyName().c_str(), captureSource->ID.c_str());
				}
			}
		}
		else {
			wasapiCapture = *it;
		}

		auto IsCaptureEnabled([&](WASAPICapture *capture) {
			return m_IsCaptureEnabled && capture->GetAudioCaptureSource()->IsEnabled && GetAudioOptions()->IsAudioEnabled();
			});

		if (wasapiCapture && startDeviceCapture) {
			if (!wasapiCapture->IsCapturing()) {
				if (IsCaptureEnabled(wasapiCapture)) {
					hr = StartDeviceCapture(wasapiCapture);
				}
			}
			else {
				if (!IsCaptureEnabled(wasapiCapture)) {
					hr = StopDeviceCapture(wasapiCapture);
				}
				else if (m_IsCapturePaused) {
					hr = PauseDeviceCapture(wasapiCapture);
				}
				else if (!m_IsCapturePaused) {
					hr = ResumeDeviceCapture(wasapiCapture);
				}
			}
		}
	}
	return hr;
}

FRAME_AUDIO_DATA *AudioManager::GrabAudioSamples()
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	std::map<WASAPICapture *, std::vector<BYTE>> audioSamples;
	INT64 start100Nanos;
	m_TimelineManager->GetMediaTimeStamp(&start100Nanos);
	UINT64 qpcTimestamp = 0;
	{
		const std::lock_guard<std::mutex> lock(WASAPICapture::StaticMutex);
		UINT64 requestedDuration100Nanos = GetNextAudioSampleDuration();
		for each (WASAPICapture * capture in m_AudioCaptures)
		{
			if (capture->IsCapturing()) {
				UINT64 qpcPos;
				std::vector<BYTE> samples = capture->GetRecordedBytesByDuration(requestedDuration100Nanos, &qpcPos);
				audioSamples.emplace(capture, samples);
				if (qpcTimestamp == 0 || qpcTimestamp > qpcPos) {
					qpcTimestamp = qpcPos;
				}
			}
		}
	}

	FRAME_AUDIO_DATA *audioData = MixAudioSamples(audioSamples);
	audioData->QpcTimestamp = qpcTimestamp;
	size_t unpaddedAudioSize = audioData->Data.size();
	if (!IsAnyAudioCapturesActive()) {
		bool paddedAudio = PadAudio(audioData->Data, m_TimelineManager->GetCurrentVideoFrameStartPosition(), m_TimelineManager->GetCurrentVideoFrameDuration());
		if (paddedAudio) {
			if (!audioData->Info) {
				audioData->Info.reset(new FRAME_AUDIO_INFO());
			}
			audioData->Info->PaddedBytes = audioData->Data.size() - unpaddedAudioSize;
			LOG_DEBUG("Padded audio with %lu/%lu bytes", audioData->Info->PaddedBytes, audioData->Data.size())
		}
	}
	return audioData;
}

INT64 AudioManager::GetNextAudioSampleDuration()
{
	INT64 lowestDuration100Nanos = 0;
	for each (WASAPICapture * capture in m_AudioCaptures)
	{
		if (IsAudioCaptureActive(capture) && !capture->NeedSync()) {
			INT64 duration = capture->GetQueuedDuration100Nanos();
			if (lowestDuration100Nanos == 0 || duration < lowestDuration100Nanos) {
				lowestDuration100Nanos = duration;
			}
		}
	}
	if (lowestDuration100Nanos == 0) {
		INT64 duration100Nanos = m_TimelineManager->GetCurrentVideoFrameDuration();
		for each (WASAPICapture * capture in m_AudioCaptures)
		{
			if (capture->IsCapturing() && capture->NeedSync()) {
				if (lowestDuration100Nanos == 0 || duration100Nanos < lowestDuration100Nanos) {
					lowestDuration100Nanos = duration100Nanos;
				}
			}
		}
	}

	if (m_TimelineManager->isMediaClockRunning()) {
		UINT64 audioSyncDiff = max(0ll, m_TimelineManager->GetNextVideoFrameStartPosition() - m_TimelineManager->GetNextAudioFrameStartPosition());
		if (audioSyncDiff < lowestDuration100Nanos) {
			lowestDuration100Nanos = audioSyncDiff;
		}
	}
	return lowestDuration100Nanos;
}

FRAME_AUDIO_DATA *AudioManager::MixAudioSamples(_In_ std::map<WASAPICapture *, std::vector<BYTE>> &audioSamples)
{
	if (audioSamples.empty()) {
		return new FRAME_AUDIO_DATA{};
	}
	std::vector<StreamData> streams{};
	streams.reserve(audioSamples.size());
	size_t maxSize = 0;
	for (auto &pair : audioSamples) {
		WASAPICapture *capture = pair.first;
		std::vector<BYTE> &data = pair.second;
		maxSize = max(maxSize, data.size());

		if (capture->GetFlow() == eCapture
		&& m_AudioOptions
		&& m_AudioOptions->GetAudioChannels() > 1
		&& capture->GetAudioCaptureSource()->ForceMono) {
			try
			{
				// This will copy the selected channel from the input device over all the output channels.
				// Useful when i.e. the input device is stereo but only outputs audio on one channel.
				data = DownmixToMono(data, capture->GetInputFormat().nChannels, m_AudioOptions->GetAudioChannels(), m_AudioOptions->GetInputMasterChannel());
				LOG_TRACE("Downmixed audio input device %s", capture->GetDeviceFriendlyName().c_str());
			}
			catch (const std::runtime_error &e) {
				LOG_ERROR("Error downmixing audio input device %s: %s.", capture->GetDeviceFriendlyName().c_str(), s2ws(e.what()).c_str());
			}
		}
		streams.push_back({
			capture->GetAudioCaptureSource()->ID,
			reinterpret_cast<const short *>(data.data()),
			data.size() / 2,
			capture->GetAudioCaptureSource()->OutputVolumeModifier
		});
	}
	const size_t maxSamples = maxSize / 2;
	const size_t streamCount = streams.size();

	std::vector<float> mixed(maxSamples, 0.0f);
	std::vector<float> streamPeak(streamCount, 0.0f);
	std::map<std::wstring, std::vector<BYTE>> sourceDataMap{};

	for (size_t s = 0; s < streamCount; ++s) {
		const short *samples = streams[s].samples;
		const size_t count = streams[s].sampleCount;
		const float volume = streams[s].volume;
		short *sourceSamples = nullptr;
		if (GetAudioOptions()->IsAudioDataPreviewEnabled()) {
			sourceDataMap.emplace(streams[s].id, std::vector<BYTE>(maxSize, 0));
			sourceSamples = reinterpret_cast<short *>(sourceDataMap[streams[s].id].data());
		}
		float peak = 0.0f;
		for (size_t i = 0; i < count; ++i) {
			short raw = samples[i];
			float sample = (raw > SILENCE_THRESHOLD || raw < -SILENCE_THRESHOLD) ? raw * volume : 0.0f;
			float sampleVolume = std::abs(sample) / 32767.0f;
			if (sampleVolume > peak) {
				peak = sampleVolume;
			}
			if (GetAudioOptions()->IsAudioDataPreviewEnabled()) {
				sourceSamples[i] += ClampSample(sample);
			}
			mixed[i] += sample;
		}
		streamPeak[s] = peak;
	}

	const float masterVolume = static_cast<float>(GetAudioOptions()->GetMasterVolume());

	std::vector<BYTE> output(maxSize);
	short *outputSamples = reinterpret_cast<short *>(output.data());
	double maxMasterGain = 0;

	for (size_t i = 0; i < maxSamples; ++i) {
		float sample = mixed[i] * masterVolume;
		double sampleVolume = std::abs(sample) / SHORT_MAX;
		if (sampleVolume > maxMasterGain) {
			maxMasterGain = sampleVolume;
		}

		outputSamples[i] = ClampSample(sample);
	}

	FRAME_AUDIO_INFO *info = new FRAME_AUDIO_INFO();
	info->Gain = maxMasterGain;
	info->Sources.reserve(streamCount);
	if (GetAudioOptions()->IsAudioDataPreviewEnabled()) {
		info->Data = output;
	}
	for (size_t s = 0; s < streamCount; ++s) {
		auto source = FRAME_AUDIO_SOURCE(streams[s].id, streamPeak[s]);
		if (GetAudioOptions()->IsAudioDataPreviewEnabled()) {
			source.Data = sourceDataMap.at(streams[s].id);
		}
		info->Sources.push_back(source);
	}
	FRAME_AUDIO_DATA *data = new FRAME_AUDIO_DATA(output, info, 0);
	return data;
}

bool AudioManager::PadAudio(_Inout_ std::vector<BYTE> &audioData, _In_ INT64 videoFramePos, _In_ INT64 videoFrameDuration)
{
	bool paddedAudio = false;
	if (GetAudioOptions()->IsAudioEnabled()
		&& audioData.size() == 0
		&& videoFrameDuration > 0) {
		INT64 expectedAudioFrames = ((videoFramePos + videoFrameDuration) * GetAudioOptions()->GetAudioSamplesPerSecond()) / 10'000'000ULL;
		INT64 renderedAudioFrames = m_TimelineManager->GetRenderedAudioFrameCount();
		if (renderedAudioFrames < expectedAudioFrames) {
			int frameCount = static_cast<int>(max(0ll, expectedAudioFrames - renderedAudioFrames));
			int byteCount = frameCount * (GetAudioOptions()->GetAudioBitsPerSample() / 8) * GetAudioOptions()->GetAudioChannels();
			audioData.insert(audioData.end(), byteCount, 0);
			paddedAudio = true;
		}
	}
	return paddedAudio;
}

short AudioManager::ClampSample(float sample)
{
	long output = std::lround(sample);
	if (output > SHORT_MAX) {
		output = SHORT_MAX - (output - SHORT_MAX) / 2;
	}
	else if (output < SHORT_MIN) {
		output = SHORT_MIN - (output - SHORT_MIN) / 2;
	}
	return static_cast<short>(output);
}

std::vector<BYTE> AudioManager::DownmixToMono(
	_In_ const std::vector<BYTE> &data,
	_In_ int inputChannels,
	_In_ int outputChannels,
	_In_ UINT32 channelToCopy
)
{
	const int bytesPerSample = 2; // PCM16
	const int inputBytesPerFrame = inputChannels * bytesPerSample;
	const int outputBytesPerFrame = outputChannels * bytesPerSample;

	if (data.size() % inputBytesPerFrame != 0) {
		throw std::runtime_error("Input not aligned to frame size");
	}
	const int frameCount = static_cast<int>(data.size() / inputBytesPerFrame);

	std::vector<BYTE> out(frameCount * outputBytesPerFrame);

	// Media Foundation introduces artifacts to the audio somewhere in the pipeline if all audio channels are bit-identical.
	// The solution found is to add a small amplitude change so they are no longer bit-identical, but it should not be audible.
	const double delta = 1.0;

	for (int frame = 0; frame < frameCount; ++frame)
	{
		const int inByteIndex = frame * inputBytesPerFrame;

		std::vector<short> inFrame(inputChannels);
		for (int c = 0; c < inputChannels; ++c)
		{
			int offset = inByteIndex + c * bytesPerSample;
			inFrame[c] = static_cast<short>(
				data[offset] | (data[offset + 1] << 8)
			);
		}
		if (channelToCopy >= inFrame.size()) {
			throw std::runtime_error("Invalid channel selected when downmixing to mono.");
		}
		short masterSample = inFrame[channelToCopy];

		// Write to all output channels
		const int outByteIndex = frame * outputBytesPerFrame;
		for (int c = 0; c < outputChannels; ++c)
		{
			short s = masterSample;

			// Channel > 0 gets slight decorrelation
			if (c > 0)
			{
				int v = static_cast<int>(s) + (int)delta;
				if (v > INT16_MAX) v = INT16_MAX;
				if (v < INT16_MIN) v = INT16_MIN;
				s = static_cast<short>(v);
			}

			int offset = outByteIndex + c * bytesPerSample;
			out[offset] = static_cast<BYTE>(s & 0xFF);
			out[offset + 1] = static_cast<BYTE>((s >> 8) & 0xFF);
		}
	}

	return out;
}
#include "AudioManager.h"
#include "cleanup.h"
#include <Functiondiscoverykeys_devpkey.h>
#include "CoreAudio.util.h"
#include <mutex>
using namespace std;

AudioManager::AudioManager() :
	m_AudioOptions(nullptr),
	m_IsCaptureEnabled(false),
	m_AudioCaptures{}
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

HRESULT AudioManager::Initialize(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions)
{
	HRESULT hr = S_OK;
	m_AudioOptions = audioOptions;
	StopOptionsChangeListenerThread();
	ResetEvent(m_OptionsListenerStopEvent);
	m_OptionsListenerThread = std::thread([this] {OnOptionsChanged(); });
	return ConfigureAudioCapture(false);
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
	if (pCapture->IsCapturing()) {
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
	const std::lock_guard<std::mutex> lock(WASAPICapture::StaticMutex);
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
			auto capture = new WASAPICapture(m_AudioOptions, captureSource);
			hr = capture->Initialize(captureSource->DeviceName, captureSource->Kind);
			if (SUCCEEDED(hr)) {
				m_AudioCaptures.insert(m_AudioCaptures.end(), capture);
				wasapiCapture = capture;
				LOG_DEBUG("Created WASAPI capture on %s with unique ID %s", wasapiCapture->GetDeviceFriendlyName().c_str(), captureSource->ID.c_str());
			}
		}
		else {
			wasapiCapture = *it;
		}

		if (wasapiCapture && startDeviceCapture) {
			if (!wasapiCapture->IsCapturing() && m_IsCaptureEnabled) {
				hr = StartDeviceCapture(wasapiCapture);
			}
			else if (wasapiCapture->IsCapturing() && !m_IsCaptureEnabled) {
				hr = StopDeviceCapture(wasapiCapture);
			}
			else if (wasapiCapture->IsCapturing() && m_IsCapturePaused) {
				hr = PauseDeviceCapture(wasapiCapture);
			}
			else if (wasapiCapture->IsCapturing() && !m_IsCapturePaused) {
				hr = ResumeDeviceCapture(wasapiCapture);
			}
		}
	}
	return hr;
}

std::vector<BYTE> AudioManager::GrabAudioSamples(_Out_ UINT64 *qpcTimestamp)
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);

	std::map<WASAPICapture *, std::vector<BYTE>> audioSamples;
	*qpcTimestamp = 0;
	int lowestFrameCount = 0;
	{
		const std::lock_guard<std::mutex> lock(WASAPICapture::StaticMutex);
		for each (WASAPICapture * capture in m_AudioCaptures)
		{
			int frameCount = capture->GetNextFrameCount();
			if (lowestFrameCount == 0 || frameCount < lowestFrameCount) {
				lowestFrameCount = frameCount;
			}
		}

		for each (WASAPICapture * capture in m_AudioCaptures)
		{
			UINT64 qpcPos;
			audioSamples.emplace(capture, capture->GetRecordedBytesByFrameCount(lowestFrameCount, &qpcPos));
			if (*qpcTimestamp == 0 || *qpcTimestamp > qpcPos) {
				*qpcTimestamp = qpcPos;
			}
		}
	}

	return MixAudioSamples(audioSamples);
}


std::vector<BYTE> AudioManager::MixAudioSamples(std::map<WASAPICapture *, std::vector<BYTE>> &audioSamples)
{
	if (audioSamples.empty()) {
		return {};
	}
	std::vector<StreamData> streams;
	streams.reserve(audioSamples.size());
	size_t maxSize = 0;
	for (auto &pair : audioSamples) {
		maxSize = max(maxSize, pair.second.size());

		if (pair.first->GetFlow() == eCapture
		&& m_AudioOptions
		&& m_AudioOptions->GetAudioChannels() > 1
		&& m_AudioOptions->IsInputDeviceDownmixingEnabled()) {
			try
			{
				// This will copy the selected channel from the input device over all the output channels.
				// Useful when i.e. the input device is stereo but only outputs audio on one channel.
				pair.second = DownmixToMono(pair.second, pair.first->GetInputFormat().nChannels, m_AudioOptions->GetAudioChannels(), m_AudioOptions->GetInputMasterChannel());
				LOG_TRACE("Downmixed input audio");
			}
			catch (const std::runtime_error &e) {
				LOG_ERROR("Error downmixing audio input device: %s.", s2ws(e.what()).c_str());
			}
		}
		streams.push_back({
			reinterpret_cast<const short *>(pair.second.data()),
			pair.second.size() / 2,
			pair.first->GetAudioCaptureSource()->OutputVolumeModifier
		});
	}

	std::vector<BYTE> output(maxSize);
	const size_t maxSamples = maxSize / 2;
	short *outputSamples = reinterpret_cast<short *>(output.data());
	bool clipped = false;

	for (size_t i = 0; i < maxSamples; i += 1) {
		int mixed = 0;
		for (auto &s : streams) {
			if (i < s.sampleCount) {
				short sample = s.samples[i];
				mixed += static_cast<int>(sample * s.volume);
			}
		}
		mixed = static_cast<int>(mixed * GetAudioOptions()->GetMasterVolume());
		if (mixed > 32767) {
			mixed = 32767 - (mixed - 32767) / 2;
			clipped = true;
		}
		else if (mixed < -32768) {
			mixed = -32768 - (mixed + 32768) / 2;
			clipped = true;
		}

		outputSamples[i] = static_cast<short>(mixed);
	}

	if (clipped) {
		LOG_WARN("Audio clipped during mixing");
	}
	return output;
}

std::vector<BYTE> AudioManager::DownmixToMono(
	_In_ const std::vector<BYTE> &data,
	_In_ int inputChannels,
	_In_ int outputChannels,
	_In_ int channelToCopy
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
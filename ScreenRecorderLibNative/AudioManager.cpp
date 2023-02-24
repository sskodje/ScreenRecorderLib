#include "AudioManager.h"
#include "cleanup.h"

using namespace std;
AudioManager::AudioManager() :
	m_AudioOptions(nullptr),
	m_IsCaptureEnabled(false)
{
	InitializeCriticalSection(&m_CriticalSection);
}

AudioManager::~AudioManager()
{
	DeleteCriticalSection(&m_CriticalSection);
}

HRESULT AudioManager::Initialize(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions)
{
	m_AudioOptions = audioOptions;
	return ConfigureAudioCapture();
}

void AudioManager::ClearRecordedBytes()
{
	if (m_LoopbackCaptureOutputDevice)
		m_LoopbackCaptureOutputDevice->ClearRecordedBytes();
	if (m_LoopbackCaptureInputDevice)
		m_LoopbackCaptureInputDevice->ClearRecordedBytes();
}

HRESULT AudioManager::StartCapture() {
	m_IsCaptureEnabled = true;
	return ConfigureAudioCapture();
}

HRESULT AudioManager::StopCapture()
{
	m_IsCaptureEnabled = false;
	return ConfigureAudioCapture();
}

HRESULT AudioManager::ConfigureAudioCapture()
{
	HRESULT hr = S_FALSE;
	if (GetAudioOptions()->IsAudioEnabled() && GetAudioOptions()->IsOutputDeviceEnabled() && m_IsCaptureEnabled)
	{
		if (!m_LoopbackCaptureOutputDevice) {
			m_LoopbackCaptureOutputDevice = make_unique<LoopbackCapture>(L"AudioOutputDevice");
			LOG_DEBUG("Created audio capture AudioOutputDevice");
		}
		if (!m_LoopbackCaptureOutputDevice->IsCapturing()) {
			hr = m_LoopbackCaptureOutputDevice->StartCapture(GetAudioOptions()->GetAudioSamplesPerSecond(), GetAudioOptions()->GetAudioChannels(), GetAudioOptions()->GetAudioOutputDevice(), eRender);
			if (SUCCEEDED(hr)) {
				LOG_DEBUG("Started audio capture on AudioOutputDevice");
			}
		}
	}
	else {
		if (m_LoopbackCaptureOutputDevice && m_LoopbackCaptureOutputDevice->IsCapturing()) {
			m_LoopbackCaptureOutputDevice->StopCapture();
			LOG_DEBUG("Stopped audio capture on AudioOutputDevice");
		}
	}

	if (GetAudioOptions()->IsAudioEnabled() && GetAudioOptions()->IsInputDeviceEnabled() && m_IsCaptureEnabled)
	{
		if (!m_LoopbackCaptureInputDevice) {
			m_LoopbackCaptureInputDevice = make_unique<LoopbackCapture>(L"AudioInputDevice");
			LOG_DEBUG("Created audio capture AudioInputDevice");
		}
		if (!m_LoopbackCaptureInputDevice->IsCapturing()) {
			hr = m_LoopbackCaptureInputDevice->StartCapture(GetAudioOptions()->GetAudioSamplesPerSecond(), GetAudioOptions()->GetAudioChannels(), GetAudioOptions()->GetAudioInputDevice(), eCapture);
			if (SUCCEEDED(hr)) {
				LOG_DEBUG("started audio capture on AudioInputDevice");
			}
		}
	}
	else {
		if (m_LoopbackCaptureInputDevice && m_LoopbackCaptureInputDevice->IsCapturing()) {
			m_LoopbackCaptureInputDevice->StopCapture();
			LOG_DEBUG("Stopped audio capture on AudioInputDevice");
		}
	}
	return hr;
}

std::vector<BYTE> AudioManager::GrabAudioFrame(_In_ UINT64 durationHundredNanos)
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	if (m_AudioOptions) {
		ConfigureAudioCapture();
	}
	if (m_LoopbackCaptureOutputDevice && m_LoopbackCaptureInputDevice) {

		auto returnAudioOverflowToBuffer = [&](auto &outputDeviceData, auto &inputDeviceData) {
			if (outputDeviceData.size() > 0 && inputDeviceData.size() > 0) {
				if (outputDeviceData.size() > inputDeviceData.size()) {
					auto diff = outputDeviceData.size() - inputDeviceData.size();
					std::vector<BYTE> overflow(outputDeviceData.end() - diff, outputDeviceData.end());
					outputDeviceData.resize(outputDeviceData.size() - diff);
					m_LoopbackCaptureOutputDevice->ReturnAudioBytesToBuffer(overflow);
				}
				else if (inputDeviceData.size() > outputDeviceData.size()) {
					auto diff = inputDeviceData.size() - outputDeviceData.size();
					std::vector<BYTE> overflow(inputDeviceData.end() - diff, inputDeviceData.end());
					inputDeviceData.resize(inputDeviceData.size() - diff);
					m_LoopbackCaptureInputDevice->ReturnAudioBytesToBuffer(overflow);
				}
			}
		};

		std::vector<BYTE> outputDeviceData = m_LoopbackCaptureOutputDevice->GetRecordedBytes(durationHundredNanos);
		std::vector<BYTE> inputDeviceData = m_LoopbackCaptureInputDevice->GetRecordedBytes(durationHundredNanos);
		returnAudioOverflowToBuffer(outputDeviceData, inputDeviceData);
		if (inputDeviceData.size() > 0 && outputDeviceData.size() && inputDeviceData.size() != outputDeviceData.size()) {
			LOG_ERROR(L"Mixing audio byte arrays with differing sizes");
		}

		return std::move(MixAudio(outputDeviceData, inputDeviceData, GetAudioOptions()->GetOutputVolume(), GetAudioOptions()->GetInputVolume()));
	}
	else if (m_LoopbackCaptureOutputDevice)
		return std::move(MixAudio(m_LoopbackCaptureOutputDevice->GetRecordedBytes(durationHundredNanos), std::vector<BYTE>(), GetAudioOptions()->GetOutputVolume(), 1.0));
	else if (m_LoopbackCaptureInputDevice)
		return std::move(MixAudio(std::vector<BYTE>(), m_LoopbackCaptureInputDevice->GetRecordedBytes(durationHundredNanos), 1.0, GetAudioOptions()->GetInputVolume()));
	else
		return std::vector<BYTE>();
}

std::vector<BYTE> AudioManager::MixAudio(_In_ std::vector<BYTE> const &first, _In_ std::vector<BYTE> const &second, _In_ float firstVolume, _In_ float secondVolume)
{
	std::vector<BYTE> newvector(max(first.size(), second.size()));
	bool clipped = false;
	for (size_t i = 0; i < newvector.size(); i += 2) {
		short firstSample = first.size() > i + 1 ? static_cast<short>(first[i] | first[i + 1] << 8) : 0;
		short secondSample = second.size() > i + 1 ? static_cast<short>(second[i] | second[i + 1] << 8) : 0;
		auto out = reinterpret_cast<short *>(&newvector[i]);
		int mixedSample = int(round((firstSample)*firstVolume + (secondSample)*secondVolume));
		if (mixedSample > MAXSHORT) {
			clipped = true;
			mixedSample = MAXSHORT;
		}
		else if (mixedSample < -MAXSHORT) {
			clipped = true;
			mixedSample = -MAXSHORT;
		}
		*out = (short)mixedSample;
	}
	if (clipped) {
		LOG_WARN("Audio clipped during mixing");
	}
	return newvector;
}
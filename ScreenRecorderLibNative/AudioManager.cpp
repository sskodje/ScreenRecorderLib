#include "AudioManager.h"


AudioManager::AudioManager() :
	m_AudioOptions(nullptr)
{

}

AudioManager::~AudioManager()
{
}

HRESULT AudioManager::Initialize(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions)
{
	m_AudioOptions = audioOptions;
	LoopbackCapture *outputCapture = nullptr;
	LoopbackCapture *inputCapture = nullptr;
	HRESULT hr = InitializeAudioCapture(&outputCapture, &inputCapture);
	if (SUCCEEDED(hr)) {
		m_LoopbackCaptureOutputDevice = std::unique_ptr<LoopbackCapture>(outputCapture);
		m_LoopbackCaptureInputDevice = std::unique_ptr<LoopbackCapture>(inputCapture);
	}
	else {
		LOG_ERROR(L"Audio capture failed to start: hr = 0x%08x", hr);
	}
	return hr;
}

void AudioManager::ClearRecordedBytes()
{
	if (m_LoopbackCaptureOutputDevice)
		m_LoopbackCaptureOutputDevice->ClearRecordedBytes();
	if (m_LoopbackCaptureInputDevice)
		m_LoopbackCaptureInputDevice->ClearRecordedBytes();
}

HRESULT AudioManager::InitializeAudioCapture(_Outptr_result_maybenull_ LoopbackCapture **outputAudioCapture, _Outptr_result_maybenull_ LoopbackCapture **inputAudioCapture)
{
	LoopbackCapture *pLoopbackCaptureOutputDevice = nullptr;
	LoopbackCapture *pLoopbackCaptureInputDevice = nullptr;
	HRESULT hr = S_FALSE;
	if (GetAudioOptions()->IsAudioEnabled()) {
		if (GetAudioOptions()->IsOutputDeviceEnabled())
		{
			pLoopbackCaptureOutputDevice = new LoopbackCapture(L"AudioOutputDevice");
			hr = pLoopbackCaptureOutputDevice->StartCapture(GetAudioOptions()->GetAudioSamplesPerSecond(), GetAudioOptions()->GetAudioChannels(), GetAudioOptions()->GetAudioOutputDevice(), eRender);
		}

		if (GetAudioOptions()->IsInputDeviceEnabled())
		{
			pLoopbackCaptureInputDevice = new LoopbackCapture(L"AudioInputDevice");
			hr = pLoopbackCaptureInputDevice->StartCapture(GetAudioOptions()->GetAudioSamplesPerSecond(), GetAudioOptions()->GetAudioChannels(), GetAudioOptions()->GetAudioInputDevice(), eCapture);
		}
	}
	*outputAudioCapture = pLoopbackCaptureOutputDevice;
	*inputAudioCapture = pLoopbackCaptureInputDevice;
	return hr;
}

std::vector<BYTE> AudioManager::GrabAudioFrame()
{
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

		std::vector<BYTE> outputDeviceData = m_LoopbackCaptureOutputDevice->GetRecordedBytes();
		std::vector<BYTE> inputDeviceData = m_LoopbackCaptureInputDevice->GetRecordedBytes();
		returnAudioOverflowToBuffer(outputDeviceData, inputDeviceData);
		if (inputDeviceData.size() > 0 && outputDeviceData.size() && inputDeviceData.size() != outputDeviceData.size()) {
			LOG_ERROR(L"Mixing audio byte arrays with differing sizes");
		}

		return std::move(MixAudio(outputDeviceData, inputDeviceData, GetAudioOptions()->GetOutputVolume(), GetAudioOptions()->GetInputVolume()));
	}
	else if (m_LoopbackCaptureOutputDevice)
		return std::move(MixAudio(m_LoopbackCaptureOutputDevice->GetRecordedBytes(), std::vector<BYTE>(), GetAudioOptions()->GetOutputVolume(), 1.0));
	else if (m_LoopbackCaptureInputDevice)
		return std::move(MixAudio(std::vector<BYTE>(), m_LoopbackCaptureInputDevice->GetRecordedBytes(), 1.0, GetAudioOptions()->GetInputVolume()));
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
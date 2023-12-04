#include "AudioManager.h"
#include "cleanup.h"
#include <Functiondiscoverykeys_devpkey.h>
#include "CoreAudio.util.h"
using namespace std;

AudioManager::AudioManager() :
	m_AudioOptions(nullptr),
	m_IsCaptureEnabled(false)
{
	InitializeCriticalSection(&m_CriticalSection);
	m_OptionsListenerStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

AudioManager::~AudioManager()
{
	StopOptionsChangeListenerThread();
	CloseHandle(m_OptionsListenerStopEvent);
	DeleteCriticalSection(&m_CriticalSection);
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
					ConfigureAudioCapture();
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
	return hr;
}

void AudioManager::ClearRecordedBytes()
{
	if (m_AudioOutputCapture)
		m_AudioOutputCapture->ClearRecordedBytes();
	if (m_AudioInputCapture)
		m_AudioInputCapture->ClearRecordedBytes();
}

HRESULT AudioManager::StartCapture() {
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	m_IsCaptureEnabled = true;
	return ConfigureAudioCapture();
}

HRESULT AudioManager::StopCapture()
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	m_IsCaptureEnabled = false;
	return ConfigureAudioCapture();
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

HRESULT AudioManager::StartDeviceCapture(WASAPICapture *pCapture, std::wstring deviceId, EDataFlow flow) {
	HRESULT hr = pCapture->StartCapture();
	if (hr == S_OK) {
		LOG_INFO(L"Started audio capture on %s: %s", pCapture->GetTag().c_str(), pCapture->GetDeviceName().c_str());
	}
	else if (hr == S_FALSE) {
		LOG_DEBUG(L"Audio capture on %s: %s is already running", pCapture->GetTag().c_str(), pCapture->GetDeviceName().c_str());
	}
	return hr;
}

HRESULT AudioManager::StopDeviceCapture(WASAPICapture *pCapture) {
	if (pCapture && pCapture->IsCapturing()) {
		RETURN_ON_BAD_HR(pCapture->StopCapture());
		LOG_DEBUG(L"Stopped audio capture on %s: %s", pCapture->GetTag().c_str(), pCapture->GetDeviceName().c_str());
	}
	return S_OK;
}

HRESULT AudioManager::ConfigureAudioCapture() {
	HRESULT hr = S_FALSE;
	if (GetAudioOptions()->IsAudioEnabled() && GetAudioOptions()->IsOutputDeviceEnabled() && m_IsCaptureEnabled)
	{
		if (!m_AudioOutputCapture) {
			m_AudioOutputCapture = make_unique<WASAPICapture>(m_AudioOptions, L"AudioOutputDevice");
			hr = m_AudioOutputCapture->Initialize(GetAudioOptions()->GetAudioOutputDevice(), eRender);
			LOG_DEBUG("Created WASAPI capture on %s", m_AudioOutputCapture->GetTag().c_str());
		}
		if (!m_AudioOutputCapture->IsCapturing()) {
			hr = StartDeviceCapture(m_AudioOutputCapture.get(), GetAudioOptions()->GetAudioOutputDevice(), eRender);
		}
	}
	else {
		hr = StopDeviceCapture(m_AudioOutputCapture.get());
	}

	if (GetAudioOptions()->IsAudioEnabled() && GetAudioOptions()->IsInputDeviceEnabled() && m_IsCaptureEnabled)
	{
		if (!m_AudioInputCapture) {
			m_AudioInputCapture = make_unique<WASAPICapture>(m_AudioOptions, L"AudioInputDevice");
			m_AudioInputCapture->Initialize(GetAudioOptions()->GetAudioInputDevice(), eCapture);
			LOG_DEBUG("Created WASAPI capture on %s", m_AudioInputCapture->GetTag().c_str());
		}
		if (!m_AudioInputCapture->IsCapturing()) {
			hr = StartDeviceCapture(m_AudioInputCapture.get(), GetAudioOptions()->GetAudioInputDevice(), eCapture);
		}
	}
	else {
		hr = StopDeviceCapture(m_AudioInputCapture.get());
	}
	return hr;
}

std::vector<BYTE> AudioManager::GrabAudioFrame(_In_ UINT64 durationHundredNanos)
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	if (m_AudioOutputCapture && m_AudioInputCapture) {
		auto returnAudioOverflowToBuffer = [&](auto &outputDeviceData, auto &inputDeviceData) {
			if (outputDeviceData.size() > 0 && inputDeviceData.size() > 0) {
				if (outputDeviceData.size() > inputDeviceData.size()) {
					auto diff = outputDeviceData.size() - inputDeviceData.size();
					std::vector<BYTE> overflow(outputDeviceData.end() - diff, outputDeviceData.end());
					outputDeviceData.resize(outputDeviceData.size() - diff);
					m_AudioOutputCapture->ReturnAudioBytesToBuffer(overflow);
				}
				else if (inputDeviceData.size() > outputDeviceData.size()) {
					auto diff = inputDeviceData.size() - outputDeviceData.size();
					std::vector<BYTE> overflow(inputDeviceData.end() - diff, inputDeviceData.end());
					inputDeviceData.resize(inputDeviceData.size() - diff);
					m_AudioInputCapture->ReturnAudioBytesToBuffer(overflow);
				}
			}
		};

		std::vector<BYTE> outputDeviceData = m_AudioOutputCapture->GetRecordedBytes(durationHundredNanos);
		std::vector<BYTE> inputDeviceData = m_AudioInputCapture->GetRecordedBytes(durationHundredNanos);
		returnAudioOverflowToBuffer(outputDeviceData, inputDeviceData);
		if (inputDeviceData.size() > 0 && outputDeviceData.size() && inputDeviceData.size() != outputDeviceData.size()) {
			LOG_ERROR(L"Mixing audio byte arrays with differing sizes");
		}

		return std::move(MixAudio(outputDeviceData, inputDeviceData, GetAudioOptions()->GetOutputVolume(), GetAudioOptions()->GetInputVolume()));
	}
	else if (m_AudioOutputCapture)
		return std::move(MixAudio(m_AudioOutputCapture->GetRecordedBytes(durationHundredNanos), std::vector<BYTE>(), GetAudioOptions()->GetOutputVolume(), 1.0));
	else if (m_AudioInputCapture)
		return std::move(MixAudio(std::vector<BYTE>(), m_AudioInputCapture->GetRecordedBytes(durationHundredNanos), 1.0, GetAudioOptions()->GetInputVolume()));
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
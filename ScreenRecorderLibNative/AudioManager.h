#pragma once
#include <vector>
#include "WASAPICapture.h"
#include "CommonTypes.h"
class AudioManager
{
public:
	AudioManager();
	~AudioManager();
	HRESULT Initialize(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions);
	void ClearRecordedBytes();
	HRESULT StartCapture();
	HRESULT StopCapture();
	std::vector<BYTE> GrabAudioFrame(_In_ UINT64 durationHundredNanos);
private:
	CRITICAL_SECTION m_CriticalSection;
	std::shared_ptr<AUDIO_OPTIONS> m_AudioOptions;
	//Output loopback capture, e.g. system audio.
	std::unique_ptr<WASAPICapture> m_AudioOutputCapture;
	//Audio input, i.e. microphone
	std::unique_ptr<WASAPICapture> m_AudioInputCapture;

	bool m_IsCaptureEnabled;

	AUDIO_OPTIONS *GetAudioOptions() { return m_AudioOptions.get(); }

	HRESULT StartDeviceCapture(WASAPICapture *pCapture, std::wstring deviceId, EDataFlow flow);
	HRESULT StopDeviceCapture(WASAPICapture *pCapture);
	HRESULT ConfigureAudioCapture();

	std::thread m_OptionsListenerThread;
	HANDLE m_OptionsListenerStopEvent = nullptr;
	void OnOptionsChanged();
	HRESULT StopOptionsChangeListenerThread();

	std::vector<BYTE> DownmixToMono(_In_ const std::vector<BYTE> &data, _In_ int inputChannels, _In_ int outputChannels, _In_ int channelToCopy);
	std::vector<BYTE> MixAudioSamples(_In_ std::vector<BYTE> &outputDeviceData, _In_ std::vector<BYTE> &inputDeviceData, _In_ float outputDeviceVolume, _In_ float inputDeviceVolume);
};
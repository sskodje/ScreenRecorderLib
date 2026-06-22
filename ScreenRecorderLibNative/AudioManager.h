#pragma once
#include <vector>
#include "WASAPICapture.h"
#include "CommonTypes.h"
#include <map>
class AudioManager
{
public:
	AudioManager();
	~AudioManager();
	HRESULT Initialize(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions);
	void ClearRecordedBytes();
	HRESULT StartCapture();
	HRESULT StopCapture();
	HRESULT PauseCapture();
	HRESULT ResumeCapture();
	std::vector<BYTE> GrabAudioSamples(_Out_ UINT64 *qpcTimestamp);
private:
	struct StreamData {
		const short *samples;
		size_t sampleCount;
		float volume;
	};

	CRITICAL_SECTION m_CriticalSection;
	std::shared_ptr<AUDIO_OPTIONS> m_AudioOptions;
	std::vector<WASAPICapture*> m_AudioCaptures;
	bool m_IsCaptureEnabled;
	bool m_IsCapturePaused;
	AUDIO_OPTIONS *GetAudioOptions() { return m_AudioOptions.get(); }

	HRESULT StartDeviceCapture(WASAPICapture *pCapture);
	HRESULT StopDeviceCapture(WASAPICapture *pCapture);
	HRESULT PauseDeviceCapture(WASAPICapture *pCapture);
	HRESULT ResumeDeviceCapture(WASAPICapture *pCapture);
	HRESULT ConfigureAudioCapture(bool startDeviceCapture);

	std::thread m_OptionsListenerThread;
	HANDLE m_OptionsListenerStopEvent = nullptr;
	void OnOptionsChanged();
	HRESULT StopOptionsChangeListenerThread();

	std::vector<BYTE> DownmixToMono(_In_ const std::vector<BYTE> &data, _In_ int inputChannels, _In_ int outputChannels, _In_ int channelToCopy);
	std::vector<BYTE> MixAudioSamples(std::map<WASAPICapture *, std::vector<BYTE>> &audioSamples);
};
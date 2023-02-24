#pragma once
#include <vector>
#include "LoopbackCapture.h"
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
	std::unique_ptr<LoopbackCapture> m_LoopbackCaptureOutputDevice;
	std::unique_ptr<LoopbackCapture> m_LoopbackCaptureInputDevice;

	bool m_IsCaptureEnabled;
	AUDIO_OPTIONS *GetAudioOptions() { return m_AudioOptions.get(); }
	HRESULT ConfigureAudioCapture();
	std::vector<BYTE> MixAudio(_In_ std::vector<BYTE> const &first, _In_ std::vector<BYTE> const &second, _In_ float firstVolume, _In_ float secondVolume);
};


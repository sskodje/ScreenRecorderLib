#pragma once
#include <vector>
#include "WASAPICapture.h"
#include "CommonTypes.h"
#include <map>
#include "TimelineManager.h"
class AudioManager
{
public:
	AudioManager();
	~AudioManager();
	HRESULT Initialize(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions, TimelineManager *pTimelineManager);
	void ClearRecordedBytes();
	HRESULT StartCapture();
	HRESULT StopCapture();
	HRESULT PauseCapture();
	HRESULT ResumeCapture();
	FRAME_AUDIO_DATA *GrabAudioSamples();
	inline bool const IsAnyAudioCapturesActive() {
		for each (WASAPICapture * source in m_AudioCaptures)
		{
			if (source->IsCapturing() && !source->IsPaused()) {
				return true;
			}
		}
		return false;
	};
private:
	struct StreamData {
		std::wstring id;
		const short *samples;
		size_t sampleCount;
		float volume;
	};

	CRITICAL_SECTION m_CriticalSection;
	std::shared_ptr<AUDIO_OPTIONS> m_AudioOptions;
	std::vector<WASAPICapture *> m_AudioCaptures;
	TimelineManager *m_TimelineManager;
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

	INT64 GetNextAudioSampleDuration();
	std::vector<BYTE> DownmixToMono(_In_ const std::vector<BYTE> &data, _In_ int inputChannels, _In_ int outputChannels, _In_ UINT32 channelToCopy);
	FRAME_AUDIO_DATA *MixAudioSamples(_In_ std::map<WASAPICapture *, std::vector<BYTE>> &audioSamples);
	short ClampSample(float sample);
	/// <summary>
	/// If the audio sources returns no data, we need to pad the PCM stream with zeros to give the media sink silence as input.
	/// If we don't, the sink writer will begin throttling video frames because it expects audio samples to be delivered, and think they are delayed.
	/// </summary>
	/// <param name="audioData"></param>
	/// <param name="videoFramePos"></param>
	/// <param name="videoFrameDuration"></param>
	/// <returns></returns>
	bool PadAudio(_Inout_ std::vector<BYTE> &audioData, _In_ INT64 videoFramePos, _In_ INT64 videoFrameDuration);
};
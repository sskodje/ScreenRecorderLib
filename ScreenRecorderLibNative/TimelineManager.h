#pragma once
#include "CommonTypes.h"
#include "Log.h"
#include "Util.h"
#include "MF.util.h"
#include "cleanup.h"
#include <atlbase.h>
#include "DynamicWait.h"
#include "AudioDriftCorrector.h"

class TimelineManager
{
public:
	TimelineManager();
	~TimelineManager();
	HRESULT Initialize(double targetVideoFrameDurationMillis, double snapshotsIntervalMillis);
	double GetTimeUntilNextFrameMillis();
	INT64 GetTimeUntilNextFrame100Nanos();
	INT64 GetTimeSinceLastFrame100Nanos();
	double GetTimeUntilNextShapshotMillis();
	INT64 GetTimeUntilNextShapshot100Nanos();
	inline double GetTargetVideoFrameDurationMillis() { return m_TargetVideoFrameDurationMillis; }
	HRESULT StartMediaClock();
	HRESULT ResumeMediaClock();
	HRESULT PauseMediaClock();
	HRESULT StopMediaClock();
	HRESULT GetMediaTimeStamp(_Out_ INT64 *pTime);
	bool isMediaClockRunning();
	bool isMediaClockPaused();
	void UpdateLastSnapshotTime();

	INT64 OnVideoFrame();
	INT64 OnAudioPacket(_In_ int frameCount, _In_ int sampleRate);

	INT64 GetNextVideoFrameStartPosition();
	INT64 GetNextAudioFrameStartPosition();

	inline int GetRenderedVideoFrameCount() { return m_RenderedVideoFrameCount; }
	inline INT64 GetRenderedAudioFrameCount() { return m_RenderedAudioFrameCount; }

private:
	UINT32 m_TargetVideoFrameDuration100Nanos;
	double m_TargetVideoFrameDurationMillis;
	double m_SnapshotIntervalMillis;
	INT64 m_SnapshotInterval100Nanos;
	INT64 m_NextAudioPacketStartPos100Nanos;
	INT64 m_NextVideoFrameStartPos100Nanos;
	INT64 m_LastPresentationClockTime;
	INT64 m_LastSnapshotTime;
	int m_RenderedVideoFrameCount;
	INT64 m_RenderedAudioFrameCount;
	INT64 m_AudioTimeRemainder;

	CComPtr<IMFPresentationTimeSource> m_TimeSrc;
	CComPtr<IMFPresentationClock> m_PresentationClock;
	std::unique_ptr<AudioDriftCorrector> m_AudioCorrector;
};


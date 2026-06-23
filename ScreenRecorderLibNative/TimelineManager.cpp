#include "TimelineManager.h"
#include <algorithm>


using namespace std;
TimelineManager::TimelineManager() :
	m_PresentationClock(nullptr),
	m_TargetVideoFrameDuration100Nanos(0),
	m_TargetVideoFrameDurationMillis(0),
	m_TimeSrc(nullptr),
	m_NextAudioPacketStartPos100Nanos(0),
	m_NextVideoFrameStartPos100Nanos(0),
	m_AudioCorrector(nullptr),
	m_RenderedFrameCount(0),
	m_LastSnapshotTime(0),
	m_LastPresentationClockTime(0),
	m_SnapshotIntervalMillis(0),
	m_SnapshotInterval100Nanos(0)
{
}

TimelineManager::~TimelineManager()
{
}

HRESULT TimelineManager::Initialize(int targetVideoFrameDurationMillis, int snapshotsIntervalMillis)
{
	m_TargetVideoFrameDurationMillis = targetVideoFrameDurationMillis;
	m_TargetVideoFrameDuration100Nanos = MillisToHundredNanos(targetVideoFrameDurationMillis);

	m_SnapshotIntervalMillis = snapshotsIntervalMillis;
	m_SnapshotInterval100Nanos = MillisToHundredNanos(snapshotsIntervalMillis);

	if (!m_TimeSrc) {
		RETURN_ON_BAD_HR(MFCreateSystemTimeSource(&m_TimeSrc));
	}
	if (!m_PresentationClock) {
		RETURN_ON_BAD_HR(MFCreatePresentationClock(&m_PresentationClock));
		RETURN_ON_BAD_HR(m_PresentationClock->SetTimeSource(m_TimeSrc));
	}
	if (!m_AudioCorrector) {
		m_AudioCorrector = make_unique<AudioDriftCorrector>();
	}
	return S_OK;
}

double TimelineManager::GetTimeUntilNextShapshotMillis()
{
	return HundredNanosToMillisDouble(GetTimeUntilNextShapshot100Nanos());
}

INT64 TimelineManager::GetTimeUntilNextShapshot100Nanos()
{
	if (m_LastSnapshotTime == 0) {
		return 0;
	}
	INT64 currentPresentationClockTime;
	GetMediaTimeStamp(&currentPresentationClockTime);
	INT64 durationSinceLastSnapshot100Nanos = currentPresentationClockTime - m_LastSnapshotTime;
	INT64 timeRemaining100Nanos = max(0, m_SnapshotInterval100Nanos - durationSinceLastSnapshot100Nanos);
	return timeRemaining100Nanos;
}

double TimelineManager::GetTimeUntilNextFrameMillis()
{
	return HundredNanosToMillisDouble(GetTimeUntilNextFrame100Nanos());
}

INT64 TimelineManager::GetTimeUntilNextFrame100Nanos()
{
	INT64 currentPresentationClockTime;
	GetMediaTimeStamp(&currentPresentationClockTime);
	INT64 durationSinceLastFrame100Nanos = currentPresentationClockTime - m_LastPresentationClockTime;
	INT64 timeRemaining100Nanos = max(0, m_TargetVideoFrameDuration100Nanos - durationSinceLastFrame100Nanos);
	return timeRemaining100Nanos;
}

HRESULT TimelineManager::StartMediaClock()
{
	return m_PresentationClock->Start(0);
}

HRESULT TimelineManager::ResumeMediaClock()
{
	return m_PresentationClock->Start(PRESENTATION_CURRENT_POSITION);
}

HRESULT TimelineManager::PauseMediaClock()
{
	return m_PresentationClock->Pause();
}

HRESULT TimelineManager::StopMediaClock()
{
	return m_PresentationClock->Stop();
}

bool TimelineManager::isMediaClockRunning()
{
	MFCLOCK_STATE state;
	m_PresentationClock->GetState(0, &state);
	return state == MFCLOCK_STATE_RUNNING;
}

bool TimelineManager::isMediaClockPaused()
{
	MFCLOCK_STATE state;
	m_PresentationClock->GetState(0, &state);
	return state == MFCLOCK_STATE_PAUSED;
}

HRESULT TimelineManager::GetMediaTimeStamp(_Out_ INT64 *pTime)
{
	return m_PresentationClock->GetTime(pTime);
}

void TimelineManager::UpdateLastSnapshotTime()
{
	INT64 timestamp;
	GetMediaTimeStamp(&timestamp);
	m_LastSnapshotTime = timestamp;
}

INT64 TimelineManager::OnVideoFrame()
{
	INT64 currentPresentationClockTime;
	RETURN_ON_BAD_HR(GetMediaTimeStamp(&currentPresentationClockTime));
	INT64 frameDuration100Nanos = currentPresentationClockTime - m_LastPresentationClockTime;
	m_NextVideoFrameStartPos100Nanos += frameDuration100Nanos;
	m_LastPresentationClockTime = currentPresentationClockTime;
	m_RenderedFrameCount++;
	return frameDuration100Nanos;
}

INT64 TimelineManager::OnAudioPacket(INT64 frameCount, INT64 sampleRate, INT64 qpcPosition)
{
	const INT64 audioDuration100Nanos = (frameCount * 10 * 1000 * 1000) / sampleRate;
	INT64 audioDriftCorrection = m_AudioCorrector->GetDriftCorrection(
GetRenderedFrameCount(),
GetNextAudioFrameStartPosition(),
GetNextVideoFrameStartPosition(),
qpcPosition);
	INT64 correctedAudioDuration100Nanos = audioDuration100Nanos + audioDriftCorrection;
	m_NextAudioPacketStartPos100Nanos += correctedAudioDuration100Nanos;
	return correctedAudioDuration100Nanos;
}


INT64 TimelineManager::GetNextVideoFrameStartPosition()
{
	return m_NextVideoFrameStartPos100Nanos;
}

INT64 TimelineManager::GetNextAudioFrameStartPosition()
{
	return m_NextAudioPacketStartPos100Nanos;
}

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
	m_RenderedVideoFrameCount(0),
	m_RenderedAudioFrameCount(0),
	m_LastSnapshotTime(0),
	m_LastPresentationClockTime(0),
	m_SnapshotIntervalMillis(0),
	m_SnapshotInterval100Nanos(0),
	m_AudioTimeRemainder(0)
{
}

TimelineManager::~TimelineManager()
{
}

HRESULT TimelineManager::Initialize(double targetVideoFrameDurationMillis, double snapshotsIntervalMillis)
{
	m_TargetVideoFrameDurationMillis = targetVideoFrameDurationMillis;
	m_TargetVideoFrameDuration100Nanos = static_cast<UINT32>(MillisToHundredNanos(targetVideoFrameDurationMillis));

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
	INT64 timeRemaining100Nanos = max(0ll, m_SnapshotInterval100Nanos - durationSinceLastSnapshot100Nanos);
	return timeRemaining100Nanos;
}

double TimelineManager::GetTimeUntilNextFrameMillis()
{
	return HundredNanosToMillisDouble(GetTimeUntilNextFrame100Nanos());
}

INT64 TimelineManager::GetTimeUntilNextFrame100Nanos()
{
	INT64 timeRemaining100Nanos = max(0ll, m_TargetVideoFrameDuration100Nanos - GetTimeSinceLastFrame100Nanos());
	return timeRemaining100Nanos;
}

INT64 TimelineManager::GetTimeSinceLastFrame100Nanos()
{
	INT64 currentPresentationClockTime;
	GetMediaTimeStamp(&currentPresentationClockTime);
	INT64 durationSinceLastFrame100Nanos = currentPresentationClockTime - m_LastPresentationClockTime;
	return durationSinceLastFrame100Nanos;
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
	m_RenderedVideoFrameCount++;
	return frameDuration100Nanos;
}

INT64 TimelineManager::OnAudioPacket(_In_ int frameCount, _In_ int sampleRate)
{
	UINT64 ticks = (UINT64)frameCount * 10000000ULL;
	m_AudioTimeRemainder += ticks;
	const INT64 audioDuration100Nanos = m_AudioTimeRemainder / sampleRate;
	m_AudioTimeRemainder %= sampleRate;
	m_NextAudioPacketStartPos100Nanos += audioDuration100Nanos;
	INT64 audioDriftCorrection = m_AudioCorrector->GetDriftCorrection(
GetRenderedVideoFrameCount(),
GetNextAudioFrameStartPosition(),
GetNextVideoFrameStartPosition());
	m_NextAudioPacketStartPos100Nanos += audioDriftCorrection;
	m_RenderedAudioFrameCount += frameCount;
	return audioDuration100Nanos + audioDriftCorrection;
}


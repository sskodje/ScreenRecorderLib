#pragma once
#include "CommonTypes.h"
class AudioDriftCorrector
{
public:
	AudioDriftCorrector();
	~AudioDriftCorrector();
	INT64 GetDriftCorrection(INT64 frameNum, INT64 nextAudioPacketStartPos100Nanos, INT64 nextVideoFrameStartPos100Nanos, INT64 audioQpcPosition);

private:
	const int CORRECTION_PERIOD_FRAME_COUNT = 1000;
	const int MINIMUM_DRIFT_TO_CORRECT_100_NANOS = 10000;

	INT64 m_AudioVideoDiffDeltaPeriodSum = 0;
	INT64 m_AudioDriftCorrection = 0;
	INT64 m_AccumulatedDriftCorrection = 0;
	INT64 m_InitialQpc = 0;
	INT64 m_LastCorrectionTimestamp = 0;
};
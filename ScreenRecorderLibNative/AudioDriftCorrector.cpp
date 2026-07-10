#include "AudioDriftCorrector.h"
#include <algorithm>

using namespace std;

AudioDriftCorrector::AudioDriftCorrector()
{
}

AudioDriftCorrector::~AudioDriftCorrector()
{
}

INT64 AudioDriftCorrector::GetDriftCorrection(INT64 frameNum, INT64 nextAudioPacketStartPos100Nanos, INT64 nextVideoFrameStartPos100Nanos)
{
	if (frameNum == 0) {
		return 0;
	}
	double audioDriftCorrectionDouble = 0;
	if (frameNum % CORRECTION_PERIOD_FRAME_COUNT == 0) {
		m_AudioVideoDiffDeltaPeriodSum += (nextVideoFrameStartPos100Nanos - nextAudioPacketStartPos100Nanos);

		INT64 periodAudioDrift = m_AudioVideoDiffDeltaPeriodSum / (CORRECTION_PERIOD_FRAME_COUNT);
		if (abs(periodAudioDrift) > MINIMUM_DRIFT_TO_CORRECT_100_NANOS) {
			audioDriftCorrectionDouble = static_cast<double>(periodAudioDrift) / CORRECTION_PERIOD_FRAME_COUNT;
			double clampSize = max(1, floor(abs(audioDriftCorrectionDouble) / 10));
			audioDriftCorrectionDouble = clamp(audioDriftCorrectionDouble, -clampSize, +clampSize);
		}

		m_AudioVideoDiffDeltaPeriodSum = 0;
		m_AudioDriftCorrection = static_cast<INT64>(round(audioDriftCorrectionDouble));
		LOG_TRACE("Period drift: %0.2f ms. Drift correction: %lld ns. Accumulated drift correction: %0.3f ms.s",
			-HundredNanosToMillisDouble(periodAudioDrift),
			m_AudioDriftCorrection * 100,
			HundredNanosToMillisDouble(m_AccumulatedDriftCorrection));
	}
	else {
		m_AudioVideoDiffDeltaPeriodSum += (nextVideoFrameStartPos100Nanos - nextAudioPacketStartPos100Nanos);
	}
	m_AccumulatedDriftCorrection += m_AudioDriftCorrection;
	return m_AudioDriftCorrection;
}

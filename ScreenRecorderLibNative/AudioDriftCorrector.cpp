#include "AudioDriftCorrector.h"
#include <algorithm>

using namespace std;

AudioDriftCorrector::AudioDriftCorrector()
{
}

AudioDriftCorrector::~AudioDriftCorrector()
{
}

HRESULT AudioDriftCorrector::Initialize()
{
	return S_OK;
}

INT64 AudioDriftCorrector::GetDriftCorrection(int frameNum, INT64 nextAudioPacketStartPos100Nanos, INT64 nextVideoFrameStartPos100Nanos, INT64 audioQpcPosition)
{
	if (m_InitialQpc == 0 && audioQpcPosition > 0) {
		m_InitialQpc = audioQpcPosition;
	}
	if (frameNum == 0) {
		return 0;
	}

	double audioDriftCorrectionDouble = 0;
	if (frameNum % CORRECTION_PERIOD_FRAME_COUNT == 0) {
		INT64 periodAudioDrift = 0;
		INT64 qpcDelta = 0;

		if (audioQpcPosition > 0) {
			INT64 qpcAdvance = audioQpcPosition - m_InitialQpc;
			qpcDelta = qpcAdvance - nextAudioPacketStartPos100Nanos;
			periodAudioDrift = qpcDelta;
			audioDriftCorrectionDouble = (static_cast<double>(qpcDelta) / CORRECTION_PERIOD_FRAME_COUNT);
			double clampSize = 50;
			audioDriftCorrectionDouble = clamp(audioDriftCorrectionDouble, -clampSize, +clampSize);
		}
		else {
			//fallback if audio device does not provide a qpc timestamp.
			periodAudioDrift = m_AudioVideoDiffDeltaPeriodSum / CORRECTION_PERIOD_FRAME_COUNT;
			if (abs(periodAudioDrift) > MINIMUM_DRIFT_TO_CORRECT_100_NANOS) {
				audioDriftCorrectionDouble = static_cast<double>(periodAudioDrift) / CORRECTION_PERIOD_FRAME_COUNT;
				double clampSize = max(1, floor(abs(audioDriftCorrectionDouble) / 5));
				audioDriftCorrectionDouble = clamp(audioDriftCorrectionDouble, -clampSize, +clampSize);
			}
		}
		m_AudioDriftCorrection = round(audioDriftCorrectionDouble);
		LOG_DEBUG("Period drift: %0.2f ms. Drift correction: %lld ns. QPC delta: %0.2f ms. Accumulated drift correction: %0.3f ms.",
			-HundredNanosToMillisDouble(periodAudioDrift),
			m_AudioDriftCorrection * 100,
			HundredNanosToMillisDouble(qpcDelta + m_AccumulatedDriftCorrection),
			HundredNanosToMillisDouble(m_AccumulatedDriftCorrection));
	}
	else {
		INT64 audioEnd = nextAudioPacketStartPos100Nanos;
		INT64 videoEnd = nextVideoFrameStartPos100Nanos;
		INT64 videoAudioDiffDelta = videoEnd - audioEnd;
		m_AudioVideoDiffDeltaPeriodSum += videoAudioDiffDelta;
	}
	m_AccumulatedDriftCorrection += m_AudioDriftCorrection;
	return m_AudioDriftCorrection;
}

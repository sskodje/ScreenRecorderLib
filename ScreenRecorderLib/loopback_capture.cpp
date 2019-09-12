//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#include <stdio.h>
#include <audioclient.h>
#include <vector>
#include "loopback_capture.h"
#include "log.h"
#include "cleanup.h"

using namespace std;
loopback_capture::loopback_capture()
{
	std::mutex(mtx);
}

loopback_capture::~loopback_capture()
{
	m_IsDestructed = true;
	Cleanup();
}
DWORD WINAPI LoopbackCaptureThreadFunction(LPVOID pContext) {
	LoopbackCaptureThreadFunctionArguments *pArgs =
		(LoopbackCaptureThreadFunctionArguments*)pContext;
	if (pArgs) {
		loopback_capture *capture = (loopback_capture*)pArgs->pCaptureInstance;
		if (capture) {
			pArgs->hr = capture->LoopbackCapture(
				pArgs->pMMDevice,
				pArgs->hFile,
				pArgs->bInt16,
				pArgs->hStartedEvent,
				pArgs->hStopEvent,
				&pArgs->nFrames,
				pArgs->flow,
				pArgs->samplerate
			);
		}
	}
	return 0;
}

HRESULT loopback_capture::LoopbackCapture(
	IMMDevice *pMMDevice,
	HMMIO hFile,
	bool bInt16,
	HANDLE hStartedEvent,
	HANDLE hStopEvent,
	PUINT32 pnFrames,
	EDataFlow flow,
	double samplerate
) {
	HRESULT hr;
	// activate an IAudioClient
	IAudioClient *pAudioClient;
	hr = pMMDevice->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL, NULL,
		(void**)&pAudioClient
	);
	if (FAILED(hr)) {
		ERR(L"IMMDevice::Activate(IAudioClient) failed: hr = 0x%08x", hr);
		return hr;
	}
	ReleaseOnExit releaseAudioClient(pAudioClient);

	// get the default device periodicity
	REFERENCE_TIME hnsDefaultDevicePeriod;
	hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
	if (FAILED(hr)) {
		ERR(L"IAudioClient::GetDevicePeriod failed: hr = 0x%08x", hr);
		return hr;
	}

	// get the default device format
	WAVEFORMATEX *pwfx;
	hr = pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr)) {
		ERR(L"IAudioClient::GetMixFormat failed: hr = 0x%08x", hr);
		return hr;
	}
	CoTaskMemFreeOnExit freeMixFormat(pwfx);

	if (bInt16) {
		// coerce int-16 wave format
		// can do this in-place since we're not changing the size of the format
		// also, the engine will auto-convert from float to int for us
		switch (pwfx->wFormatTag) {
		case WAVE_FORMAT_IEEE_FLOAT:
			pwfx->wFormatTag = WAVE_FORMAT_PCM;
			pwfx->wBitsPerSample = 16;
			pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
			pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
			break;

		case WAVE_FORMAT_EXTENSIBLE:
		{
			// naked scope for case-local variable
			PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
			if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat)) {
				pEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
				pEx->Samples.wValidBitsPerSample = 16;
				pwfx->wBitsPerSample = 16;
				pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
				pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
			}
			else {
				ERR(L"%s", L"Don't know how to coerce mix format to int-16");
				return E_UNEXPECTED;
			}
		}
		break;

		default:
			ERR(L"Don't know how to coerce WAVEFORMATEX with wFormatTag = 0x%08x to int-16", pwfx->wFormatTag);
			return E_UNEXPECTED;
		}
	}

	if (samplerate != 0.0)
	{
		OutSampleRate = samplerate;
	}
	else
	{
		if (pwfx->nSamplesPerSec >= 48000)
		{
			OutSampleRate = 48000.0;
		}
		else
		{
			OutSampleRate = 44100.0;
		}
	}

	m_SamplesPerSec = OutSampleRate;

	// create a periodic waitable timer
	HANDLE hWakeUp = CreateWaitableTimer(NULL, FALSE, NULL);
	if (NULL == hWakeUp) {
		DWORD dwErr = GetLastError();
		ERR(L"CreateWaitableTimer failed: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	CloseHandleOnExit closeWakeUp(hWakeUp);

	UINT32 nBlockAlign = pwfx->nBlockAlign;
	*pnFrames = 0;
	long audioClientBuffer = 200 * 10000;
	// call IAudioClient::Initialize
	// note that AUDCLNT_STREAMFLAGS_LOOPBACK and AUDCLNT_STREAMFLAGS_EVENTCALLBACK
	// do not work together...
	// the "data ready" event never gets set
	// so we're going to do a timer-driven loop
	switch (flow)
	{
	case eRender:
		hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, audioClientBuffer, 0, pwfx, 0);
		break;
	case eCapture:
		hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, audioClientBuffer, 0, pwfx, 0);
		break;
	default:
		hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, audioClientBuffer, 0, pwfx, 0);
		break;
	}
	if (FAILED(hr)) {
		ERR(L"IAudioClient::Initialize failed: hr = 0x%08x", hr);
		return hr;
	}

	// activate an IAudioCaptureClient
	IAudioCaptureClient *pAudioCaptureClient;
	hr = pAudioClient->GetService(
		__uuidof(IAudioCaptureClient),
		(void**)&pAudioCaptureClient
	);
	if (FAILED(hr)) {
		ERR(L"IAudioClient::GetService(IAudioCaptureClient) failed: hr = 0x%08x", hr);
		return hr;
	}
	ReleaseOnExit releaseAudioCaptureClient(pAudioCaptureClient);

	// register with MMCSS
	DWORD nTaskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristics(L"Audio", &nTaskIndex);
	if (NULL == hTask) {
		DWORD dwErr = GetLastError();
		ERR(L"AvSetMmThreadCharacteristics failed: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	AvRevertMmThreadCharacteristicsOnExit unregisterMmcss(hTask);

	// set the waitable timer
	LARGE_INTEGER liFirstFire;
	liFirstFire.QuadPart = -hnsDefaultDevicePeriod / 2; // negative means relative time
	LONG lTimeBetweenFires = (LONG)hnsDefaultDevicePeriod / 2 / (10 * 1000); // convert to milliseconds
	BOOL bOK = SetWaitableTimer(
		hWakeUp,
		&liFirstFire,
		lTimeBetweenFires,
		NULL, NULL, FALSE
	);
	if (!bOK) {
		DWORD dwErr = GetLastError();
		ERR(L"SetWaitableTimer failed: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	CancelWaitableTimerOnExit cancelWakeUp(hWakeUp);

	// call IAudioClient::Start
	hr = pAudioClient->Start();
	if (FAILED(hr)) {
		ERR(L"IAudioClient::Start failed: hr = 0x%08x", hr);
		return hr;
	}
	AudioClientStopOnExit stopAudioClient(pAudioClient);

	SetEvent(hStartedEvent);

	// loopback capture loop
	HANDLE waitArray[2] = { hStopEvent, hWakeUp };
	DWORD dwWaitResult;

	bool bDone = false;
	bool bFirstPacket = true;

	for (UINT32 nPasses = 0; !bDone; nPasses++) {
		// drain data while it is available
		UINT32 nNextPacketSize;
		for (
			hr = pAudioCaptureClient->GetNextPacketSize(&nNextPacketSize);
			SUCCEEDED(hr) && nNextPacketSize > 0;
			hr = pAudioCaptureClient->GetNextPacketSize(&nNextPacketSize)
			) {
			// get the captured data
			BYTE *pData;
			UINT32 nNumFramesToRead;
			DWORD dwFlags;

			hr = pAudioCaptureClient->GetBuffer(
				&pData,
				&nNumFramesToRead,
				&dwFlags,

				NULL,
				NULL
			);
			if (FAILED(hr)) {
				ERR(L"IAudioCaptureClient::GetBuffer failed on pass %u after %u frames: hr = 0x%08x", nPasses, *pnFrames, hr);
				bDone = true;
				continue; // exits loop
			}

			if (bFirstPacket && AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY == dwFlags) {
				LOG(L"%ls", L"Probably spurious glitch reported on first packet");
			}
			else if (AUDCLNT_BUFFERFLAGS_SILENT == dwFlags) {
				//LOG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames", dwFlags, nPasses, *pnFrames);
			}
			else if (0 != dwFlags) {
				LOG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames", dwFlags, nPasses, *pnFrames);
			}

			if (0 == nNumFramesToRead) {
				ERR(L"IAudioCaptureClient::GetBuffer said to read 0 frames on pass %u after %u frames", nPasses, *pnFrames);
				hr = E_UNEXPECTED;
				bDone = true;
				continue; // exits loop
			}

			LONG lBytesToWrite = nNumFramesToRead * nBlockAlign;
#pragma prefast(suppress: __WARNING_INCORRECT_ANNOTATION, "IAudioCaptureClient::GetBuffer SAL annotation implies a 1-byte buffer")

			if (m_IsDestructed) { return E_ABORT; }
			// Convert audio
			//if (pwfx->nSamplesPerSec != OutSampleRate) {
				/*
				 * In this section or in loopback_capture::GetRecordedBytes() 
				 * we should resample our audio if pwfx->nSamplesPerSec != 48000 or 44100.
				 * For this task I tried to use WWMFResampler (example can be found here https://github.com/openhome/ohPlayer/blob/master/Win32/WWMFResampler.cpp)
				 * but with this resampler I have sound distortion. So my suggestion is to use https://github.com/avaneev/r8brain-free-src.
				 * Unfortunately I can't get it work right yet.	
				 *
				 */

				//double* opp = nullptr;
				//double* InBufs = new double[lBytesToWrite];
				
				//// Convert the buffer to doubles (before resampling)
				//const double div = (1.0f / 32768.0f);
				//for (int i = 0; i < lBytesToWrite; i++) {
				//	InBufs[i] = div * (double)pData[i];
				//}
				
				//int bytesToWrite = 0;
				
				//bytesToWrite = Resampler->process(InBufs, lBytesToWrite, opp);
				
				//mtx.lock();
				//BYTE* convertedBytes = new BYTE[bytesToWrite];
				
				//// Convert back to byte
				//const double mul = (32768.0f);
				//for (int i = 0; i < bytesToWrite; i++) {
				//	double tmp = mul * InBufs[i];
				//	tmp = std::fmax(tmp, -32768); // CLIP < 32768
				//	tmp = std::fmin(tmp, 32767); // CLIP > 32767 
				//	convertedBytes[i] = (BYTE)(tmp);
				//}
				
				//if (m_RecordedBytes.size() == 0)
				//	m_RecordedBytes.reserve(bytesToWrite);
				//m_RecordedBytes.insert(m_RecordedBytes.end(), &convertedBytes[0], &convertedBytes[bytesToWrite]);
				
				//delete[] convertedBytes;
				//delete[] InBufs;
				//mtx.unlock();
			//}
			//else {
				mtx.lock();
				int size = lBytesToWrite;
				if (m_RecordedBytes.size() == 0)
					m_RecordedBytes.reserve(size);
				m_RecordedBytes.insert(m_RecordedBytes.end(), &pData[0], &pData[size]);
				mtx.unlock();
			//}

			*pnFrames += nNumFramesToRead;

			hr = pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
			if (FAILED(hr)) {
				ERR(L"IAudioCaptureClient::ReleaseBuffer failed on pass %u after %u frames: hr = 0x%08x", nPasses, *pnFrames, hr);
				bDone = true;
				continue; // exits loop
			}

			bFirstPacket = false;
		}

		if (FAILED(hr)) {
			ERR(L"IAudioCaptureClient::GetNextPacketSize failed on pass %u after %u frames: hr = 0x%08x", nPasses, *pnFrames, hr);
			bDone = true;
			continue; // exits loop
		}

		dwWaitResult = WaitForMultipleObjects(
			ARRAYSIZE(waitArray), waitArray,
			FALSE, INFINITE
		);

		if (WAIT_OBJECT_0 == dwWaitResult) {
			LOG(L"Received stop event after %u passes and %u frames", nPasses, *pnFrames);
			bDone = true;
			continue; // exits loop
		}

		if (WAIT_OBJECT_0 + 1 != dwWaitResult) {
			ERR(L"Unexpected WaitForMultipleObjects return value %u on pass %u after %u frames", dwWaitResult, nPasses, *pnFrames);
			hr = E_UNEXPECTED;
			bDone = true;
			continue; // exits loop
		}
	} // capture loop
	return hr;
}
std::vector<BYTE> loopback_capture::GetRecordedBytes()
{
	mtx.lock();
	std::vector<BYTE> newvector(m_RecordedBytes);
	m_RecordedBytes.clear();
	mtx.unlock();
	return newvector;
}

UINT32 loopback_capture::GetInputSampleRate() {
	return m_SamplesPerSec;
}


void loopback_capture::ClearRecordedBytes()
{
	mtx.lock();
	m_RecordedBytes.clear();
	mtx.unlock();
}

void loopback_capture::Cleanup()
{
	m_RecordedBytes.clear();
	vector<BYTE>().swap(m_RecordedBytes);
}
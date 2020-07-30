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
				pArgs->samplerate,
				pArgs->channels,
				pArgs->tag
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
	UINT32 samplerate,
	UINT32 channels,
	LPCWSTR tag
) {
	m_Tag = tag;
	HRESULT hr;
	if (pMMDevice == nullptr) {
		ERR(L"IMMDevice is NULL");
		return E_FAIL;
	}
	// activate an IAudioClient
	IAudioClient *pAudioClient;
	hr = pMMDevice->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL, NULL,
		(void**)&pAudioClient
	);
	if (FAILED(hr)) {
		ERR(L"IMMDevice::Activate(IAudioClient) failed on %s: hr = 0x%08x", tag, hr);
		return hr;
	}
	ReleaseOnExit releaseAudioClient(pAudioClient);

	// get the default device periodicity
	REFERENCE_TIME hnsDefaultDevicePeriod;
	hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
	if (FAILED(hr)) {
		ERR(L"IAudioClient::GetDevicePeriod failed on %s: hr = 0x%08x", tag, hr);
		return hr;
	}

	// get the default device format
	WAVEFORMATEX *pwfx;
	hr = pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr)) {
		ERR(L"IAudioClient::GetMixFormat failed on %s: hr = 0x%08x", tag, hr);
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

	// set resampler options
	if (samplerate != 0)
	{
		m_SamplesPerSec = samplerate;
	}
	else
	{
		if (pwfx->nSamplesPerSec >= 48000)
		{
			m_SamplesPerSec = 48000;
		}
		else
		{
			m_SamplesPerSec = 44100;
		}
	}

	inputFormat.nChannels = pwfx->nChannels;
	inputFormat.bits = pwfx->wBitsPerSample;
	inputFormat.sampleRate = pwfx->nSamplesPerSec;
	inputFormat.dwChannelMask = 0;
	inputFormat.validBitsPerSample = pwfx->wBitsPerSample;
	inputFormat.sampleFormat = WWMFBitFormatInt;

	outputFormat = inputFormat;
	outputFormat.sampleRate = m_SamplesPerSec;
	outputFormat.nChannels = channels;

	// initialize resampler if sample rate differs from 44.1kHz or 48kHz
	if (requiresResampling()) {
		LOG("Resampler (bits): %u -> %u", inputFormat.bits, outputFormat.bits);
		LOG("Resampler (channels): %u -> %u", inputFormat.nChannels, outputFormat.nChannels);
		LOG("Resampler (sampleFormat): %i -> %i", inputFormat.sampleFormat, outputFormat.sampleFormat);
		LOG("Resampler (sampleRate): %lu -> %lu", inputFormat.sampleRate, outputFormat.sampleRate);
		LOG("Resampler (validBitsPerSample): %u -> %u", inputFormat.validBitsPerSample, outputFormat.validBitsPerSample);
		resampler.Initialize(inputFormat, outputFormat, 60);
	}
	else
	{
		LOG("No resampling nescessary");
	}

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
		ERR(L"IAudioClient::Initialize failed on %s: hr = 0x%08x", tag, hr);
		return hr;
	}

	// activate an IAudioCaptureClient
	IAudioCaptureClient *pAudioCaptureClient;
	hr = pAudioClient->GetService(
		__uuidof(IAudioCaptureClient),
		(void**)&pAudioCaptureClient
	);
	if (FAILED(hr)) {
		ERR(L"IAudioClient::GetService(IAudioCaptureClient) failed on %s: hr = 0x%08x", tag, hr);
		return hr;
	}
	ReleaseOnExit releaseAudioCaptureClient(pAudioCaptureClient);

	// register with MMCSS
	DWORD nTaskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristics(L"Audio", &nTaskIndex);
	if (NULL == hTask) {
		DWORD dwErr = GetLastError();
		ERR(L"AvSetMmThreadCharacteristics failed on %s: last error = %u", tag, dwErr);
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
		ERR(L"SetWaitableTimer failed on %s: last error = %u", tag, dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	CancelWaitableTimerOnExit cancelWakeUp(hWakeUp);

	// call IAudioClient::Start
	hr = pAudioClient->Start();
	if (FAILED(hr)) {
		ERR(L"IAudioClient::Start failed on %s: hr = 0x%08x", tag, hr);
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
			m_IsCapturing = true;
			hr = pAudioCaptureClient->GetBuffer(
				&pData,
				&nNumFramesToRead,
				&dwFlags,

				NULL,
				NULL
			);
			if (FAILED(hr)) {
				ERR(L"IAudioCaptureClient::GetBuffer failed on pass %u after %u frames on %s: hr = 0x%08x", nPasses, *pnFrames, tag, hr);
				bDone = true;
				continue; // exits loop
			}

			if (bFirstPacket && AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY == dwFlags) {
				LOG(L"Probably spurious glitch reported on first packet on %s", tag);
			}
			else if (AUDCLNT_BUFFERFLAGS_SILENT == dwFlags) {
				//LOG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames", dwFlags, nPasses, *pnFrames);
			}
			else if (0 != dwFlags) {
				LOG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames on %s", dwFlags, nPasses, *pnFrames, tag);
			}

			if (0 == nNumFramesToRead) {
				ERR(L"IAudioCaptureClient::GetBuffer said to read 0 frames on pass %u after %u frames on %s", nPasses, *pnFrames, tag);
				hr = E_UNEXPECTED;
				bDone = true;
				continue; // exits loop
			}

			LONG lBytesToWrite = nNumFramesToRead * nBlockAlign;
#pragma prefast(suppress: __WARNING_INCORRECT_ANNOTATION, "IAudioCaptureClient::GetBuffer SAL annotation implies a 1-byte buffer")

			if (m_IsDestructed) { return E_ABORT; }

			mtx.lock();
			int size = lBytesToWrite;
			if (m_RecordedBytes.size() == 0)
				m_RecordedBytes.reserve(size);
			m_RecordedBytes.insert(m_RecordedBytes.end(), &pData[0], &pData[size]);
			mtx.unlock();

			*pnFrames += nNumFramesToRead;

			hr = pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
			if (FAILED(hr)) {
				ERR(L"IAudioCaptureClient::ReleaseBuffer failed on pass %u after %u frames on %s: hr = 0x%08x", nPasses, *pnFrames, tag, hr);
				bDone = true;
				continue; // exits loop
			}

			bFirstPacket = false;
		}
		m_IsCapturing = false;
		if (FAILED(hr)) {
			ERR(L"IAudioCaptureClient::GetNextPacketSize failed on pass %u after %u frames on %s: hr = 0x%08x", nPasses, *pnFrames, tag, hr);
			bDone = true;
			continue; // exits loop
		}

		dwWaitResult = WaitForMultipleObjects(
			ARRAYSIZE(waitArray), waitArray,
			FALSE, INFINITE
		);

		if (WAIT_OBJECT_0 == dwWaitResult) {
			LOG(L"Received stop event after %u passes and %u frames on %s", nPasses, *pnFrames, tag);
			bDone = true;
			continue; // exits loop
		}

		if (WAIT_OBJECT_0 + 1 != dwWaitResult) {
			ERR(L"Unexpected WaitForMultipleObjects return value %u on pass %u after %u frames on %s", dwWaitResult, nPasses, *pnFrames, tag);
			hr = E_UNEXPECTED;
			bDone = true;
			continue; // exits loop
		}
	} // capture loop
	return hr;
}
std::vector<BYTE> loopback_capture::PeakRecordedBytes()
{
	return m_RecordedBytes;
}

std::vector<BYTE> loopback_capture::GetRecordedBytes()
{
	return GetRecordedBytes(-1);
}
std::vector<BYTE> loopback_capture::GetRecordedBytes(int byteCount)
{
	if (byteCount > 0) {
		while (m_RecordedBytes.size() < byteCount && m_IsCapturing) {
			LOG(L"Waiting for more audio on %s", m_Tag);
			Sleep(1);
		}
		byteCount = min(byteCount, m_RecordedBytes.size());
	}
	else if (byteCount <= 0) {
		byteCount = m_RecordedBytes.size();
	}
	mtx.lock();

	std::vector<BYTE> newvector(m_RecordedBytes.begin(), m_RecordedBytes.begin() + byteCount);
	// convert audio
	if (requiresResampling()) {

		HRESULT hr = resampler.Resample(newvector.data(), newvector.size(), &sampleData);
		if (SUCCEEDED(hr)) {
			LOG(L"Resampled audio from %dch %uhz to %dch %uhz", inputFormat.nChannels, inputFormat.sampleRate, outputFormat.nChannels, outputFormat.sampleRate);
		}
		else {
			ERR(L"Resampling of audio failed: hr = 0x%08x", hr);
		}
		newvector.clear();

		newvector.insert(newvector.end(), &sampleData.data[0], &sampleData.data[sampleData.bytes]);

		sampleData.Release();
	}
	m_RecordedBytes.erase(m_RecordedBytes.begin(), m_RecordedBytes.begin() + byteCount);
	//	LOG(L"Got %d bytes from loopback_capture %s. %d bytes remaining", newvector.size(), m_Tag, m_RecordedBytes.size());
	mtx.unlock();
	return newvector;
}

UINT32 loopback_capture::GetInputSampleRate() {
	return m_SamplesPerSec;
}

bool loopback_capture::requiresResampling()
{
	return inputFormat.sampleRate != outputFormat.sampleRate || outputFormat.nChannels != inputFormat.nChannels;
}

bool loopback_capture::IsCapturing() {
	return m_IsCapturing;
}

void loopback_capture::ClearRecordedBytes()
{
	mtx.lock();
	m_RecordedBytes.clear();
	mtx.unlock();
}

void loopback_capture::Cleanup()
{
	if (requiresResampling()) {
		resampler.Finalize();
	}
	m_RecordedBytes.clear();
	vector<BYTE>().swap(m_RecordedBytes);
}
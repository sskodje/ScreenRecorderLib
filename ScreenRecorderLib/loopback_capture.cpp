//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#include <stdio.h>
#include <audioclient.h>
#include <vector>
#include "loopback_capture.h"
#include "log.h"
#include "cleanup.h"
#include "audio_prefs.h"
#include <thread>

using namespace std;
loopback_capture::loopback_capture(std::wstring tag)
{
	loopback_capture();
	m_Tag = tag;
}
loopback_capture::loopback_capture()
{
	std::mutex(mtx);
	m_Tag = L"";
}
loopback_capture::~loopback_capture()
{
	CloseHandle(m_CaptureStopEvent);
	CloseHandle(m_CaptureStartedEvent);
}

HRESULT loopback_capture::LoopbackCapture(
	IMMDevice *pMMDevice,
	HMMIO hFile,
	bool bInt16,
	HANDLE hStartedEvent,
	HANDLE hStopEvent,
	EDataFlow flow,
	UINT32 samplerate,
	UINT32 channels
) {
	HRESULT hr;
	if (pMMDevice == nullptr) {
		ERROR(L"IMMDevice is NULL");
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
		ERROR(L"IMMDevice::Activate(IAudioClient) failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}
	ReleaseOnExit releaseAudioClient(pAudioClient);

	// get the default device periodicity
	REFERENCE_TIME hnsDefaultDevicePeriod;
	hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
	if (FAILED(hr)) {
		ERROR(L"IAudioClient::GetDevicePeriod failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}

	// get the default device format
	WAVEFORMATEX *pwfx;
	hr = pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr)) {
		ERROR(L"IAudioClient::GetMixFormat failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
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
				ERROR(L"%s", L"Don't know how to coerce mix format to int-16");
				return E_UNEXPECTED;
			}
		}
		break;

		default:
			ERROR(L"Don't know how to coerce WAVEFORMATEX with wFormatTag = 0x%08x to int-16", pwfx->wFormatTag);
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

	m_InputFormat.nChannels = pwfx->nChannels;
	m_InputFormat.bits = pwfx->wBitsPerSample;
	m_InputFormat.sampleRate = pwfx->nSamplesPerSec;
	m_InputFormat.dwChannelMask = 0;
	m_InputFormat.validBitsPerSample = pwfx->wBitsPerSample;
	m_InputFormat.sampleFormat = WWMFBitFormatInt;

	m_OutputFormat = m_InputFormat;
	m_OutputFormat.sampleRate = m_SamplesPerSec;
	m_OutputFormat.nChannels = channels;

	// initialize resampler if sample rate differs from 44.1kHz or 48kHz
	if (requiresResampling()) {
		DEBUG("Resampler (bits): %u -> %u", m_InputFormat.bits, m_OutputFormat.bits);
		DEBUG("Resampler (channels): %u -> %u", m_InputFormat.nChannels, m_OutputFormat.nChannels);
		DEBUG("Resampler (sampleFormat): %i -> %i", m_InputFormat.sampleFormat, m_OutputFormat.sampleFormat);
		DEBUG("Resampler (sampleRate): %lu -> %lu", m_InputFormat.sampleRate, m_OutputFormat.sampleRate);
		DEBUG("Resampler (validBitsPerSample): %u -> %u", m_InputFormat.validBitsPerSample, m_OutputFormat.validBitsPerSample);
		m_Resampler.Initialize(m_InputFormat, m_OutputFormat, 60);
	}
	else
	{
		DEBUG("No resampling nescessary");
	}

	// create a periodic waitable timer
	HANDLE hWakeUp = CreateWaitableTimer(NULL, FALSE, NULL);
	if (NULL == hWakeUp) {
		DWORD dwErr = GetLastError();
		ERROR(L"CreateWaitableTimer failed: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	CloseHandleOnExit closeWakeUp(hWakeUp);

	UINT32 nBlockAlign = pwfx->nBlockAlign;
	UINT32 nFrames = 0;
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
		ERROR(L"IAudioClient::Initialize failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}

	// activate an IAudioCaptureClient
	IAudioCaptureClient *pAudioCaptureClient;
	hr = pAudioClient->GetService(
		__uuidof(IAudioCaptureClient),
		(void**)&pAudioCaptureClient
	);
	if (FAILED(hr)) {
		ERROR(L"IAudioClient::GetService(IAudioCaptureClient) failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}
	ReleaseOnExit releaseAudioCaptureClient(pAudioCaptureClient);

	// register with MMCSS
	DWORD nTaskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristics(L"Audio", &nTaskIndex);
	if (NULL == hTask) {
		DWORD dwErr = GetLastError();
		ERROR(L"AvSetMmThreadCharacteristics failed on %ls: last error = %u", m_Tag.c_str(), dwErr);
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
		ERROR(L"SetWaitableTimer failed on %ls: last error = %u", m_Tag.c_str(), dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	CancelWaitableTimerOnExit cancelWakeUp(hWakeUp);

	// call IAudioClient::Start
	hr = pAudioClient->Start();
	if (FAILED(hr)) {
		ERROR(L"IAudioClient::Start failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
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
				ERROR(L"IAudioCaptureClient::GetBuffer failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, m_Tag.c_str(), hr);
				bDone = true;
				continue; // exits loop
			}

			if (bFirstPacket && AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY == dwFlags) {
				DEBUG(L"Probably spurious glitch reported on first packet on %ls", m_Tag.c_str());
			}
			else if (AUDCLNT_BUFFERFLAGS_SILENT == dwFlags) {
				//INFO(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames", dwFlags, nPasses, *pnFrames);
			}
			else if (0 != dwFlags) {
				DEBUG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames on %ls", dwFlags, nPasses, nFrames, m_Tag.c_str());
			}

			if (0 == nNumFramesToRead) {
				ERROR(L"IAudioCaptureClient::GetBuffer said to read 0 frames on pass %u after %u frames on %ls", nPasses, nFrames, m_Tag.c_str());
				hr = E_UNEXPECTED;
				bDone = true;
				continue; // exits loop
			}

			LONG lBytesToWrite = nNumFramesToRead * nBlockAlign;
#pragma prefast(suppress: __WARNING_INCORRECT_ANNOTATION, "IAudioCaptureClient::GetBuffer SAL annotation implies a 1-byte buffer")

			m_Mutex.lock();
			int size = lBytesToWrite;
			if (m_RecordedBytes.size() == 0)
				m_RecordedBytes.reserve(size);
			m_RecordedBytes.insert(m_RecordedBytes.end(), &pData[0], &pData[size]);
			m_Mutex.unlock();

			nFrames += nNumFramesToRead;

			hr = pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
			if (FAILED(hr)) {
				ERROR(L"IAudioCaptureClient::ReleaseBuffer failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, m_Tag.c_str(), hr);
				bDone = true;
				continue; // exits loop
			}

			bFirstPacket = false;
		}
		m_IsCapturing = false;
		if (FAILED(hr)) {
			ERROR(L"IAudioCaptureClient::GetNextPacketSize failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, m_Tag.c_str(), hr);
			bDone = true;
			continue; // exits loop
		}

		dwWaitResult = WaitForMultipleObjects(
			ARRAYSIZE(waitArray), waitArray,
			FALSE, INFINITE
		);

		if (WAIT_OBJECT_0 == dwWaitResult) {
			DEBUG(L"Received stop event after %u passes and %u frames on %ls", nPasses, nFrames, m_Tag.c_str());
			bDone = true;
			continue; // exits loop
		}

		if (WAIT_OBJECT_0 + 1 != dwWaitResult) {
			ERROR(L"Unexpected WaitForMultipleObjects return value %u on pass %u after %u frames on %ls", dwWaitResult, nPasses, nFrames, m_Tag.c_str());
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
			TRACE(L"Waiting for more audio on %ls", m_Tag.c_str());
			Sleep(1);
		}
		byteCount = min(byteCount, m_RecordedBytes.size());
	}
	else if (byteCount <= 0) {
		byteCount = m_RecordedBytes.size();
	}
	m_Mutex.lock();

	std::vector<BYTE> newvector(m_RecordedBytes.begin(), m_RecordedBytes.begin() + byteCount);
	// convert audio
	if (requiresResampling()) {
		HRESULT hr = m_Resampler.Resample(newvector.data(), newvector.size(), &m_SampleData);
		if (SUCCEEDED(hr)) {
			TRACE(L"Resampled audio from %dch %uhz to %dch %uhz", m_InputFormat.nChannels, m_InputFormat.sampleRate, m_OutputFormat.nChannels, m_OutputFormat.sampleRate);
		}
		else {
			ERROR(L"Resampling of audio failed: hr = 0x%08x", hr);
		}
		newvector.clear();

		newvector.insert(newvector.end(), &m_SampleData.data[0], &m_SampleData.data[m_SampleData.bytes]);

		m_SampleData.Release();
	}
	m_RecordedBytes.erase(m_RecordedBytes.begin(), m_RecordedBytes.begin() + byteCount);
	TRACE(L"Got %d bytes from loopback_capture %ls. %d bytes remaining", newvector.size(), m_Tag.c_str(), m_RecordedBytes.size());
	m_Mutex.unlock();
	return newvector;
}

HRESULT loopback_capture::StartCapture(UINT32 sampleRate, UINT32 audioChannels, std::wstring device)
{
	HRESULT hr = E_FAIL;
	bool isDeviceEmpty = device.empty();
	LPCWSTR argv[3] = { L"", L"--device", device.c_str() };
	int argc = isDeviceEmpty ? 1 : SIZEOF_ARRAY(argv);
	CPrefs prefs(argc, isDeviceEmpty ? nullptr : argv, hr, eRender);
	if (SUCCEEDED(hr)) {
		m_CaptureStartedEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (nullptr == m_CaptureStartedEvent) {
			ERROR(L"CreateEvent failed: last error is %u", GetLastError());
			return E_FAIL;
		}

		m_CaptureStopEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (nullptr == m_CaptureStopEvent) {
			ERROR(L"CreateEvent failed: last error is %u", GetLastError());
			return E_FAIL;
		}
		IMMDevice *device = prefs.m_pMMDevice;
		auto file = prefs.m_hFile;
		m_CaptureTask = concurrency::create_task([this, sampleRate, audioChannels, device, file]() {
			LoopbackCapture(device,
				file,
				true,
				m_CaptureStartedEvent,
				m_CaptureStopEvent,
				eRender,
				sampleRate,
				audioChannels);
		});
		hr = WaitForSingleObjectEx(m_CaptureStartedEvent, 1000, false);
	}
	return hr;
}

HRESULT loopback_capture::StopCapture()
{
	SetEvent(m_CaptureStopEvent);
	if (requiresResampling()) {
		m_Resampler.Finalize();
	}
	m_CaptureTask.wait();
	return S_OK;
}

UINT32 loopback_capture::GetInputSampleRate() {
	return m_SamplesPerSec;
}

bool loopback_capture::requiresResampling()
{
	return m_InputFormat.sampleRate != m_OutputFormat.sampleRate || m_OutputFormat.nChannels != m_InputFormat.nChannels;
}

bool loopback_capture::IsCapturing() {
	return m_IsCapturing;
}

void loopback_capture::ClearRecordedBytes()
{
	m_Mutex.lock();
	m_RecordedBytes.clear();
	m_Mutex.unlock();
}
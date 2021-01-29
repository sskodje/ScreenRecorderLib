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
	std::mutex(mtx);
	m_Tag = tag;
}
loopback_capture::loopback_capture()
{
	std::mutex(mtx);
	m_Tag = L"";
}
loopback_capture::~loopback_capture()
{
	StopCapture();
	CloseHandle(m_CaptureStopEvent);
	CloseHandle(m_CaptureStartedEvent);
	m_Resampler.Finalize();
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
		LOG_ERROR(L"IMMDevice is NULL");
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
		LOG_ERROR(L"IMMDevice::Activate(IAudioClient) failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}
	ReleaseOnExit releaseAudioClient(pAudioClient);

	// get the default device periodicity
	REFERENCE_TIME hnsDefaultDevicePeriod;
	hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
	if (FAILED(hr)) {
		LOG_ERROR(L"IAudioClient::GetDevicePeriod failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}

	// get the default device format
	WAVEFORMATEX *pwfx;
	hr = pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr)) {
		LOG_ERROR(L"IAudioClient::GetMixFormat failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
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
				LOG_ERROR(L"%s", L"Don't know how to coerce mix format to int-16");
				return E_UNEXPECTED;
			}
		}
		break;

		default:
			LOG_ERROR(L"Don't know how to coerce WAVEFORMATEX with wFormatTag = 0x%08x to int-16", pwfx->wFormatTag);
			return E_UNEXPECTED;
		}
	}
	UINT32 outputSampleRate;
	// set resampler options
	if (samplerate != 0)
	{
		outputSampleRate = samplerate;
	}
	else
	{
		if (pwfx->nSamplesPerSec >= 48000)
		{
			outputSampleRate = 48000;
		}
		else
		{
			outputSampleRate = 44100;
		}
	}

	m_InputFormat.nChannels = pwfx->nChannels;
	m_InputFormat.bits = pwfx->wBitsPerSample;
	m_InputFormat.sampleRate = pwfx->nSamplesPerSec;
	m_InputFormat.dwChannelMask = 0;
	m_InputFormat.validBitsPerSample = pwfx->wBitsPerSample;
	m_InputFormat.sampleFormat = WWMFBitFormatInt;

	m_OutputFormat = m_InputFormat;
	m_OutputFormat.sampleRate = outputSampleRate;
	m_OutputFormat.nChannels = channels;

	// initialize resampler if input sample rate or channels are different from output.
	if (requiresResampling()) {
		LOG_DEBUG("Resampler created for %ls", m_Tag.c_str());
		LOG_DEBUG("Resampler (bits): %u -> %u", m_InputFormat.bits, m_OutputFormat.bits);
		LOG_DEBUG("Resampler (channels): %u -> %u", m_InputFormat.nChannels, m_OutputFormat.nChannels);
		LOG_DEBUG("Resampler (sampleFormat): %i -> %i", m_InputFormat.sampleFormat, m_OutputFormat.sampleFormat);
		LOG_DEBUG("Resampler (sampleRate): %lu -> %lu", m_InputFormat.sampleRate, m_OutputFormat.sampleRate);
		LOG_DEBUG("Resampler (validBitsPerSample): %u -> %u", m_InputFormat.validBitsPerSample, m_OutputFormat.validBitsPerSample);
		m_Resampler.Initialize(m_InputFormat, m_OutputFormat, 60);
	}
	else
	{
		LOG_DEBUG("No resampling nescessary");
	}

	// create a periodic waitable timer
	HANDLE hWakeUp = CreateWaitableTimer(NULL, FALSE, NULL);
	if (NULL == hWakeUp) {
		DWORD dwErr = GetLastError();
		LOG_ERROR(L"CreateWaitableTimer failed: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	CloseHandleOnExit closeWakeUp(hWakeUp);

	UINT32 nBlockAlign = pwfx->nBlockAlign;
	UINT32 nFrames = 0;
	long audioClientBuffer = 200 * 10000; //200ms in 100-nanosecond units.
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
		LOG_ERROR(L"IAudioClient::Initialize failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}

	// activate an IAudioCaptureClient
	IAudioCaptureClient *pAudioCaptureClient;
	hr = pAudioClient->GetService(
		__uuidof(IAudioCaptureClient),
		(void**)&pAudioCaptureClient
	);
	if (FAILED(hr)) {
		LOG_ERROR(L"IAudioClient::GetService(IAudioCaptureClient) failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}
	ReleaseOnExit releaseAudioCaptureClient(pAudioCaptureClient);

	// register with MMCSS
	DWORD nTaskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristics(L"Audio", &nTaskIndex);
	if (NULL == hTask) {
		DWORD dwErr = GetLastError();
		LOG_ERROR(L"AvSetMmThreadCharacteristics failed on %ls: last error = %u", m_Tag.c_str(), dwErr);
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
		LOG_ERROR(L"SetWaitableTimer failed on %ls: last error = %u", m_Tag.c_str(), dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	CancelWaitableTimerOnExit cancelWakeUp(hWakeUp);

	// call IAudioClient::Start
	hr = pAudioClient->Start();
	if (FAILED(hr)) {
		LOG_ERROR(L"IAudioClient::Start failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}
	AudioClientStopOnExit stopAudioClient(pAudioClient);

	SetEvent(hStartedEvent);

	// loopback capture loop
	HANDLE waitArray[2] = { hStopEvent, hWakeUp };
	DWORD dwWaitResult;

	bool bDone = false;
	bool bFirstPacket = true;
	UINT64 nLastDevicePosition = 0;
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
			UINT64 nDevicePosition;
			m_IsCapturing = true;
			hr = pAudioCaptureClient->GetBuffer(
				&pData,
				&nNumFramesToRead,
				&dwFlags,
				&nDevicePosition,
				NULL
			);
			if (FAILED(hr)) {
				LOG_ERROR(L"IAudioCaptureClient::GetBuffer failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, m_Tag.c_str(), hr);
				bDone = true;
				continue; // exits loop
			}
			bool isDiscontinuity = false;
			if ((dwFlags & (AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)) != 0) {
				if (bFirstPacket) {
					LOG_DEBUG(L"Probably spurious glitch reported on first packet on %ls", m_Tag.c_str());
				}
				else {
					LOG_DEBUG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames on %ls", dwFlags, nPasses, nFrames, m_Tag.c_str());
					isDiscontinuity = true;
				}
			}
			else if ((dwFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
				//Captured data should be replaced with silence as according to https://docs.microsoft.com/en-us/windows/win32/coreaudio/capturing-a-stream
				LOG_DEBUG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames on %ls", dwFlags, nPasses, nFrames, m_Tag.c_str());
				memset(pData, 0, sizeof(pData));
			}
			else if (0 != dwFlags) {
				LOG_DEBUG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames on %ls", dwFlags, nPasses, nFrames, m_Tag.c_str());
			}

			if (0 == nNumFramesToRead) {
				LOG_ERROR(L"IAudioCaptureClient::GetBuffer said to read 0 frames on pass %u after %u frames on %ls", nPasses, nFrames, m_Tag.c_str());
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
			//This should reduce glitching if there is discontinuity in the audio stream.
			if (isDiscontinuity) {
				int frameDiff = nDevicePosition - nLastDevicePosition;
				if (frameDiff != nNumFramesToRead) {
					m_RecordedBytes.insert(m_RecordedBytes.begin(), frameDiff * nBlockAlign, 0);
					LOG_DEBUG(L"Discontinuity detected, padded audio bytes with %d bytes of silence on %ls", frameDiff, m_Tag.c_str());
				}
			}
			m_Mutex.unlock();

			nFrames += nNumFramesToRead;

			hr = pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
			if (FAILED(hr)) {
				LOG_ERROR(L"IAudioCaptureClient::ReleaseBuffer failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, m_Tag.c_str(), hr);
				bDone = true;
				continue; // exits loop
			}
			bFirstPacket = false;
			nLastDevicePosition = nDevicePosition;
		}
		m_IsCapturing = false;
		if (FAILED(hr)) {
			LOG_ERROR(L"IAudioCaptureClient::GetNextPacketSize failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, m_Tag.c_str(), hr);
			bDone = true;
			continue; // exits loop
		}

		dwWaitResult = WaitForMultipleObjects(
			ARRAYSIZE(waitArray), waitArray,
			FALSE, INFINITE
		);

		if (WAIT_OBJECT_0 == dwWaitResult) {
			LOG_DEBUG(L"Received stop event after %u passes and %u frames on %ls", nPasses, nFrames, m_Tag.c_str());
			bDone = true;
			continue; // exits loop
		}

		if (WAIT_OBJECT_0 + 1 != dwWaitResult) {
			LOG_ERROR(L"Unexpected WaitForMultipleObjects return value %u on pass %u after %u frames on %ls", dwWaitResult, nPasses, nFrames, m_Tag.c_str());
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
	auto byteCount = m_RecordedBytes.size();
	m_Mutex.lock();

	std::vector<BYTE> newvector(m_RecordedBytes.begin(), m_RecordedBytes.begin() + byteCount);
	// convert audio
	if (requiresResampling() && byteCount > 0) {
		WWMFSampleData sampleData;
		HRESULT hr = m_Resampler.Resample(newvector.data(), newvector.size(), &sampleData);
		if (SUCCEEDED(hr)) {
			LOG_TRACE(L"Resampled audio from %dch %uhz to %dch %uhz", m_InputFormat.nChannels, m_InputFormat.sampleRate, m_OutputFormat.nChannels, m_OutputFormat.sampleRate);
		}
		else {
			LOG_ERROR(L"Resampling of audio failed: hr = 0x%08x", hr);
		}
		newvector.clear();
		newvector.insert(newvector.end(), &sampleData.data[0], &sampleData.data[sampleData.bytes]);
		sampleData.Release();
	}
	if (m_OverflowBytes.size() > 0) {
		newvector.insert(newvector.begin(), m_OverflowBytes.begin(), m_OverflowBytes.end());
		m_OverflowBytes.clear();
	}
	m_RecordedBytes.erase(m_RecordedBytes.begin(), m_RecordedBytes.begin() + byteCount);
	LOG_TRACE(L"Got %d bytes from loopback_capture %ls. %d bytes remaining", newvector.size(), m_Tag.c_str(), m_RecordedBytes.size());
	m_Mutex.unlock();
	return newvector;
}

HRESULT loopback_capture::StartCapture(UINT32 sampleRate, UINT32 audioChannels, std::wstring device, EDataFlow flow)
{
	HRESULT hr = E_FAIL;
	bool isDeviceEmpty = device.empty();
	LPCWSTR argv[3] = { L"", L"--device", device.c_str() };
	int argc = isDeviceEmpty ? 1 : SIZEOF_ARRAY(argv);
	CPrefs prefs(argc, isDeviceEmpty ? nullptr : argv, hr, flow);
	if (SUCCEEDED(hr)) {
		m_CaptureStartedEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (nullptr == m_CaptureStartedEvent) {
			LOG_ERROR(L"CreateEvent failed: last error is %u", GetLastError());
			return E_FAIL;
		}

		m_CaptureStopEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (nullptr == m_CaptureStopEvent) {
			LOG_ERROR(L"CreateEvent failed: last error is %u", GetLastError());
			return E_FAIL;
		}

		IMMDevice *device = prefs.m_pMMDevice;
		auto file = prefs.m_hFile;
		m_CaptureTask = concurrency::create_task([this, flow, sampleRate, audioChannels, device, file]() {
			LoopbackCapture(device,
				file,
				true,
				m_CaptureStartedEvent,
				m_CaptureStopEvent,
				flow,
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
	m_CaptureTask.wait();
	return S_OK;
}

void loopback_capture::ReturnAudioBytesToBuffer(std::vector<BYTE> bytes)
{
	m_OverflowBytes.swap(bytes);
	LOG_TRACE(L"Returned %d bytes to buffer in loopback_capture %ls", m_OverflowBytes.size(), m_Tag.c_str());
}

bool loopback_capture::requiresResampling()
{
	return m_InputFormat.sampleRate != m_OutputFormat.sampleRate
		|| m_InputFormat.nChannels != m_OutputFormat.nChannels;
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
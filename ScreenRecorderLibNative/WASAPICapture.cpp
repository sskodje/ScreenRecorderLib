//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#include "Cleanup.h"
#include "WASAPICapture.h"
#include <mutex>
#include <ppltasks.h> 
#include "CoreAudio.util.h"
#include "DynamicWait.h"
#include "WASAPINotify.h"
using namespace std;

struct WASAPICapture::TaskWrapper {
	std::mutex m_Mutex;
	CComPtr<WASAPINotify> m_Notify;
	std::thread m_CaptureThread;
	std::thread m_ReconnectThread;
};

WASAPICapture::WASAPICapture(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions, _In_opt_ std::wstring tag) :
	m_DeviceId(L""),
	m_DeviceName(L""),
	m_DefaultDeviceId(L""),
	m_Resampler(nullptr),
	m_pEnumerator(nullptr),
	m_Flow(eRender),
	m_IsDefaultDevice(false)
{
	m_Tag = tag;
	m_AudioOptions = audioOptions;
	m_TaskWrapperImpl = make_unique<TaskWrapper>();
	m_TaskWrapperImpl->m_Notify = new WASAPINotify(this);
	m_CaptureStartedEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	m_CaptureStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	m_CaptureRestartEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	m_ReconnectThreadStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	m_CaptureReconnectEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

	ResetEvent(m_AudioOptions->OnPropertyChangedEvent);
	m_TaskWrapperImpl->m_ReconnectThread = std::thread([this] {ReconnectThreadLoop(); });
	m_RetryWait.SetWaitBands({
							  {0, 1},
							  {10, 3},
							  {100, 5},
							  {500, 10},
							  {3000, WAIT_BAND_STOP}
							});
	StartListeners();
}

WASAPICapture::~WASAPICapture()
{
	StopListeners();
	StopReconnectThread();
	StopCapture();
	CloseHandle(m_CaptureStopEvent);
	CloseHandle(m_CaptureStartedEvent);
	CloseHandle(m_CaptureRestartEvent);
	CloseHandle(m_CaptureReconnectEvent);
	CloseHandle(m_ReconnectThreadStopEvent);
}

HRESULT WASAPICapture::Initialize(_In_ std::wstring deviceId, _In_ EDataFlow flow) {
	m_Flow = flow;
	CComPtr<IMMDevice> pDevice = nullptr;
	if (deviceId.empty() || m_IsDefaultDevice) {
		m_IsDefaultDevice = true;
		RETURN_ON_BAD_HR(GetDefaultAudioDevice(flow, &pDevice));
	}
	else {
		RETURN_ON_BAD_HR(GetActiveAudioDevice(deviceId.c_str(), flow, &pDevice));
	}

	if (pDevice) {
		LPWSTR deviceId;
		pDevice->GetId(&deviceId);
		m_DeviceId = std::wstring(deviceId);
		if (m_IsDefaultDevice) {
			m_DefaultDeviceId = m_DeviceId;
		}
		GetAudioDeviceFriendlyName(deviceId, &m_DeviceName);
		CoTaskMemFree(deviceId);
	}
	else {
		LOG_ERROR("IMMDevice cannot be NULL");
		return E_FAIL;
	}

	HRESULT hr = InitializeAudioClient(pDevice, &m_AudioClient);
	if (SUCCEEDED(hr)) {
		WWMFResampler *pResampler;
		hr = InitializeResampler(m_AudioOptions->GetAudioSamplesPerSecond(), m_AudioOptions->GetAudioChannels(), m_AudioClient, &m_InputFormat, &m_OutputFormat, &pResampler);
		if (SUCCEEDED(hr)) {
			m_Resampler.reset(pResampler);
		}
	}
	return hr;
}

HRESULT WASAPICapture::InitializeAudioClient(
	_In_ IMMDevice *pMMDevice,
	_Outptr_ IAudioClient **ppAudioClient)
{
	*ppAudioClient = nullptr;
	if (pMMDevice == nullptr) {
		LOG_ERROR(L"IMMDevice is NULL");
		return E_FAIL;
	}
	// activate an IAudioClient
	CComPtr<IAudioClient> pAudioClient = nullptr;
	HRESULT hr = pMMDevice->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL, NULL,
		(void **)&pAudioClient
	);
	if (FAILED(hr)) {
		LOG_ERROR(L"IMMDevice::Activate(IAudioClient) failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}
	WAVEFORMATEX *pwfx;
	RETURN_ON_BAD_HR(GetWaveFormat(pAudioClient, true, &pwfx));
	CoTaskMemFreeOnExit freeMixFormat(pwfx);

	EDataFlow flow;
	GetAudioDeviceFlow(pMMDevice, &flow);
	switch (flow)
	{
		case eRender:
			hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, AUDIO_CLIENT_BUFFER_100_NS, 0, pwfx, 0);
			break;
		case eCapture:
			hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, AUDIO_CLIENT_BUFFER_100_NS, 0, pwfx, 0);
			break;
		default:
			hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, AUDIO_CLIENT_BUFFER_100_NS, 0, pwfx, 0);
			break;
	}
	if (FAILED(hr)) {
		LOG_ERROR(L"IAudioClient::Initialize failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}
	*ppAudioClient = pAudioClient;
	(*ppAudioClient)->AddRef();
	return hr;
}

HRESULT WASAPICapture::InitializeResampler(
	_In_ UINT32 samplerate,
	_In_ UINT32 nChannels,
	_In_ IAudioClient *pAudioClient,
	_Out_ WWMFPcmFormat *audioInputFormat,
	_Out_ WWMFPcmFormat *audioOutputFormat,
	_Outptr_result_maybenull_ WWMFResampler **ppResampler)
{
	*ppResampler = nullptr;
	WWMFPcmFormat inputFormat = {};
	WWMFPcmFormat outputFormat = {};
	UINT32 outputSampleRate;

	WAVEFORMATEX *pwfx;
	RETURN_ON_BAD_HR(GetWaveFormat(pAudioClient, true, &pwfx));
	CoTaskMemFreeOnExit freeMixFormat(pwfx);

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

	inputFormat.nChannels = pwfx->nChannels;
	inputFormat.bits = pwfx->wBitsPerSample;
	inputFormat.sampleRate = pwfx->nSamplesPerSec;
	inputFormat.dwChannelMask = 0;
	inputFormat.validBitsPerSample = pwfx->wBitsPerSample;
	inputFormat.sampleFormat = WWMFBitFormatType::WWMFBitFormatInt;

	outputFormat = inputFormat;
	outputFormat.sampleRate = outputSampleRate;
	outputFormat.nChannels = nChannels;

	*audioInputFormat = inputFormat;
	*audioOutputFormat = outputFormat;

	bool requiresResampling = inputFormat.sampleRate != outputFormat.sampleRate
		|| inputFormat.nChannels != outputFormat.nChannels;
	// initialize resampler if input sample rate or channels are different from output.
	if (requiresResampling) {
		LOG_DEBUG("Resampler created for %ls", m_Tag.c_str());
		LOG_DEBUG("Resampler (bits): %u -> %u", inputFormat.bits, outputFormat.bits);
		LOG_DEBUG("Resampler (channels): %u -> %u", inputFormat.nChannels, outputFormat.nChannels);
		LOG_DEBUG("Resampler (sampleFormat): %i -> %i", inputFormat.sampleFormat, outputFormat.sampleFormat);
		LOG_DEBUG("Resampler (sampleRate): %lu -> %lu", inputFormat.sampleRate, outputFormat.sampleRate);
		LOG_DEBUG("Resampler (validBitsPerSample): %u -> %u", inputFormat.validBitsPerSample, outputFormat.validBitsPerSample);
		*ppResampler = new WWMFResampler();
		(*ppResampler)->Initialize(inputFormat, outputFormat, 60);
		return S_OK;
	}
	else
	{
		LOG_DEBUG("No resampling necessary");
		return S_FALSE;
	}
}

HRESULT WASAPICapture::GetWaveFormat(
	_In_ IAudioClient *pAudioClient,
	_In_ bool bInt16,
	_Out_ WAVEFORMATEX **pWaveFormat) {
	// get the default device format
	WAVEFORMATEX *pwfx;
	HRESULT hr = pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr)) {
		LOG_ERROR(L"IAudioClient::GetMixFormat failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
		return hr;
	}

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
				}}
			break;

			default:
				LOG_ERROR(L"Don't know how to coerce WAVEFORMATEX with wFormatTag = 0x%08x to int-16", pwfx->wFormatTag);
				return E_UNEXPECTED;
		}
	}
	*pWaveFormat = pwfx;
	return hr;
}

HRESULT WASAPICapture::StartCaptureLoop(
		_In_ IAudioClient *pAudioClient,
		_In_ HANDLE hStartedEvent,
		_In_ HANDLE hStopEvent,
		_In_ HANDLE hRestartEvent
) {
	HRESULT hr = S_OK;
	WAVEFORMATEX *pwfx;
	RETURN_ON_BAD_HR(hr = GetWaveFormat(pAudioClient, true, &pwfx));
	CoTaskMemFreeOnExit freeMixFormat(pwfx);
	UINT32 nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
	UINT32 nFrames = 0;

	int bufferFrameCount = int(ceil(pwfx->nSamplesPerSec * HundredNanosToSeconds(AUDIO_CLIENT_BUFFER_100_NS)));
	int bufferByteCount = bufferFrameCount * nBlockAlign;
	BYTE *bufferData = new BYTE[bufferByteCount]{ 0 };
	DeleteArrayOnExit deleteBufferData(bufferData);
	{
		// activate an IAudioCaptureClient
		CComPtr<IAudioCaptureClient> pAudioCaptureClient = nullptr;
		hr = pAudioClient->GetService(
			__uuidof(IAudioCaptureClient),
			(void **)&pAudioCaptureClient
		);
		if (FAILED(hr)) {
			LOG_ERROR(L"IAudioClient::GetService(IAudioCaptureClient) failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
			return hr;
		}

		// get the default device periodicity
		REFERENCE_TIME hnsDefaultDevicePeriod;
		hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
		if (FAILED(hr)) {
			LOG_ERROR(L"IAudioClient::GetDevicePeriod failed on %ls: hr = 0x%08x", m_Tag.c_str(), hr);
			return hr;
		}

		// create a periodic waitable timer
		HANDLE hWakeUp = CreateWaitableTimer(NULL, FALSE, NULL);
		if (NULL == hWakeUp) {
			DWORD dwErr = GetLastError();
			LOG_ERROR(L"CreateWaitableTimer failed: last error = %u", dwErr);
			return HRESULT_FROM_WIN32(dwErr);
		}
		CloseHandleOnExit closeWakeUp(hWakeUp);

		// set the waitable timer
		LARGE_INTEGER liFirstFire{};
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
		HANDLE waitArray[3] = { hStopEvent, hRestartEvent, hWakeUp };
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

				UINT32 size = nNumFramesToRead * nBlockAlign;
				memcpy_s(bufferData, bufferByteCount, pData, size);

				hr = pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
				if (FAILED(hr)) {
					LOG_ERROR(L"IAudioCaptureClient::ReleaseBuffer failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, m_Tag.c_str(), hr);
					bDone = true;
					continue; // exits loop
				}
#pragma warning(disable: 26110)
				const std::lock_guard<std::mutex> lock(m_TaskWrapperImpl->m_Mutex);
#pragma prefast(suppress: __WARNING_INCORRECT_ANNOTATION, "IAudioCaptureClient::GetBuffer SAL annotation implies a 1-byte buffer")
				if (m_RecordedBytes.size() == 0)
					m_RecordedBytes.reserve(size);
				m_RecordedBytes.insert(m_RecordedBytes.end(), &bufferData[0], &bufferData[size]);
				//This should reduce glitching if there is discontinuity in the audio stream.
				if (isDiscontinuity) {
					UINT64 frameDiff = nDevicePosition - nLastDevicePosition;
					if (frameDiff != nNumFramesToRead) {
						m_RecordedBytes.insert(m_RecordedBytes.begin(), (size_t)(frameDiff * nBlockAlign), 0);
						LOG_DEBUG(L"Discontinuity detected, padded audio bytes with %d bytes of silence on %ls", frameDiff, m_Tag.c_str());
					}
				}
				nFrames += nNumFramesToRead;
				bFirstPacket = false;
				nLastDevicePosition = nDevicePosition;
			}

			if (FAILED(hr)) {
				LOG_ERROR(L"IAudioCaptureClient::GetNextPacketSize failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, m_Tag.c_str(), hr);
				bDone = true;
				continue; // exits loop
			}

			dwWaitResult = WaitForMultipleObjects(ARRAYSIZE(waitArray), waitArray, FALSE, 5000);

			if (WAIT_OBJECT_0 == dwWaitResult) {
				LOG_DEBUG(L"Received stop event after %u passes and %u frames on %ls", nPasses, nFrames, m_Tag.c_str());
				bDone = true;
			}
			else if (WAIT_OBJECT_0 + 1 == dwWaitResult) {
				LOG_DEBUG(L"Received restart event after %u passes and %u frames on %ls", nPasses, nFrames, m_Tag.c_str());
				bDone = true;
			}
			else if (WAIT_TIMEOUT == dwWaitResult) {
				LOG_ERROR(L"WaitForMultipleObjects timeout on pass %u after %u frames on %ls", dwWaitResult, nPasses, nFrames, m_Tag.c_str());
				hr = E_UNEXPECTED;
				bDone = true;
			}
			else if (WAIT_OBJECT_0 + 2 != dwWaitResult) {
				LOG_ERROR(L"Unexpected WaitForMultipleObjects return value %u on pass %u after %u frames on %ls", dwWaitResult, nPasses, nFrames, m_Tag.c_str());
				hr = E_UNEXPECTED;
				bDone = true;
			}
		} // capture loop
	}
#pragma warning(disable: 26117)
	return hr;
}
std::vector<BYTE> WASAPICapture::PeakRecordedBytes()
{
	return m_RecordedBytes;
}

std::vector<BYTE> WASAPICapture::GetRecordedBytes(UINT64 duration100Nanos)
{
	int frameCount = int(ceil(m_InputFormat.sampleRate * HundredNanosToSeconds(duration100Nanos)));
	std::vector<BYTE> newvector;
	size_t byteCount;
	{
		const std::lock_guard<std::mutex> lock(m_TaskWrapperImpl->m_Mutex);
		byteCount = min((frameCount * m_InputFormat.FrameBytes()), m_RecordedBytes.size());
		newvector = std::vector<BYTE>(m_RecordedBytes.begin(), m_RecordedBytes.begin() + byteCount);
		m_RecordedBytes.erase(m_RecordedBytes.begin(), m_RecordedBytes.begin() + byteCount);
		LOG_TRACE(L"Got %d bytes from WASAPICapture %ls. %d bytes remaining", newvector.size(), m_Tag.c_str(), m_RecordedBytes.size());

		// convert audio
		if (m_Resampler && byteCount > 0) {
			WWMFSampleData sampleData;
			HRESULT hr = m_Resampler->Resample(newvector.data(), (DWORD)newvector.size(), &sampleData);
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
	}
	if (m_OverflowBytes.size() > 0) {
		newvector.insert(newvector.begin(), m_OverflowBytes.begin(), m_OverflowBytes.end());
		m_OverflowBytes.clear();
	}

	return newvector;
}

HRESULT WASAPICapture::StartCapture()
{
	const std::lock_guard<std::mutex> lock(m_TaskWrapperImpl->m_Mutex);
	if (m_IsCapturing.load()) {
		return S_FALSE;
	}
	ResetEvent(m_CaptureReconnectEvent);
	if (m_IsOffline.load()) {
		return E_ABORT;
	}
	if (!m_AudioClient) {
		HRESULT hr = Initialize(m_DeviceId, m_Flow);
		if (FAILED(hr)) {
			if (hr == E_NOTFOUND) {
				SetOffline(true);
			}
			return hr;
		}
		m_RecordedBytes.clear();
	}
	if (m_TaskWrapperImpl->m_CaptureThread.joinable()) {
		SetEvent(m_CaptureStopEvent);
		m_TaskWrapperImpl->m_CaptureThread.join();
	}

	ResetEvent(m_CaptureRestartEvent);
	ResetEvent(m_CaptureStopEvent);
	ResetEvent(m_CaptureStartedEvent);
	m_TaskWrapperImpl->m_CaptureThread = std::thread([&]() {
		LOG_TRACE("WASAPICapture thread started");
		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		// register with MMCSS
		DWORD nTaskIndex = 0;
		HANDLE hTask = AvSetMmThreadCharacteristics(L"Audio", &nTaskIndex);
		if (NULL == hTask) {
			DWORD dwErr = GetLastError();
			LOG_ERROR(L"AvSetMmThreadCharacteristics failed on %ls: last error = %u", m_Tag.c_str(), dwErr);
			return HRESULT_FROM_WIN32(dwErr);
		}
		AvRevertMmThreadCharacteristicsOnExit unregisterMmcss(hTask);
		try {
			if (SUCCEEDED(hr)) {
				hr = StartCaptureLoop(m_AudioClient, m_CaptureStartedEvent, m_CaptureStopEvent, m_CaptureRestartEvent);
			}
			if (FAILED(hr)) {
				LOG_ERROR(L"Audio capture loop failed to start: hr = 0x%08x", hr);
			}
		}
		catch (const exception &e) {
			LOG_ERROR(L"Exception in WASAPICapture: %s", s2ws(e.what()).c_str());
		}
		catch (...) {
			LOG_ERROR(L"Exception in WASAPICapture");
		}
		m_IsCapturing.store(false);
		CoUninitialize();
		m_AudioClient.Release();
		bool isStop = WaitForSingleObjectEx(m_CaptureStopEvent, 0, FALSE) == WAIT_OBJECT_0;
		bool isRestart = WaitForSingleObjectEx(m_CaptureRestartEvent, 0, FALSE) == WAIT_OBJECT_0;

		if (!isStop || isRestart) {
			SetEvent(m_CaptureReconnectEvent);
		}
		SetEvent(m_CaptureStopEvent);
		LOG_TRACE("WASAPICapture thread exited");
		return hr;
	});
	HANDLE events[2] = { m_CaptureStartedEvent ,m_CaptureStopEvent };
	DWORD dwWaitResult = WaitForMultipleObjects(ARRAYSIZE(events), events, false, INFINITE);
	if (dwWaitResult == WAIT_OBJECT_0) {
		m_IsCapturing.store(true);
		ResetEvent(m_CaptureReconnectEvent);
	}
	else if (dwWaitResult == WAIT_OBJECT_0 + 1) {
		LOG_ERROR(L"Received stop event when starting capture");
		return E_FAIL;
	}
	else if (dwWaitResult == WAIT_TIMEOUT) {
		LOG_ERROR(L"Timed out when starting capture");
		return E_FAIL;
	}
	return S_OK;
}

HRESULT WASAPICapture::StopCapture()
{
	SetEvent(m_CaptureStopEvent);
	try
	{
		if (m_TaskWrapperImpl->m_CaptureThread.joinable()) {
			m_TaskWrapperImpl->m_CaptureThread.join();
		}
		else {
			return S_FALSE;
		}
	}
	catch (const exception &e) {
		LOG_ERROR(L"Exception in StopCapture: %s", s2ws(e.what()).c_str());
		return E_FAIL;
	}
	catch (...) {
		LOG_ERROR(L"Exception in StopCapture");
	}
	return S_OK;
}

HRESULT WASAPICapture::StopReconnectThread()
{
	SetEvent(m_ReconnectThreadStopEvent);
	m_RetryWait.Cancel();
	try
	{
		if (m_TaskWrapperImpl->m_ReconnectThread.joinable()) {
			m_TaskWrapperImpl->m_ReconnectThread.join();
		}
		else {
			return S_FALSE;
		}
	}
	catch (const exception &e) {
		LOG_ERROR(L"Exception in StopReconnectThread: %s", s2ws(e.what()).c_str());
		return E_FAIL;
	}
	catch (...) {
		LOG_ERROR(L"Exception in StopReconnectThread");
	}
	return S_OK;
}

bool WASAPICapture::StartListeners() {
	if (!m_IsRegisteredForEndpointNotifications)
	{
		// Create the device enumerator
		IMMDeviceEnumerator *pEnumerator;
		HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&pEnumerator);
		if (SUCCEEDED(hr)) {
			// Register for device change notifications
			hr = pEnumerator->RegisterEndpointNotificationCallback(m_TaskWrapperImpl->m_Notify);
			m_pEnumerator = pEnumerator;
			m_IsRegisteredForEndpointNotifications = true;
			return true;
		}
	}
	return false;
}

bool WASAPICapture::StopListeners() {
	// Unregister the device enumerator
	if (m_IsRegisteredForEndpointNotifications) {
		if (m_pEnumerator) {
			m_pEnumerator->UnregisterEndpointNotificationCallback(m_TaskWrapperImpl->m_Notify);
		}
	}
	m_TaskWrapperImpl->m_Notify.Release();
	return true;
}

void WASAPICapture::ReturnAudioBytesToBuffer(std::vector<BYTE> bytes)
{
	m_OverflowBytes.swap(bytes);
	LOG_TRACE(L"Returned %d bytes to buffer in WASAPICapture %ls", m_OverflowBytes.size(), m_Tag.c_str());
}

void WASAPICapture::SetDefaultDevice(EDataFlow flow, ERole role, LPCWSTR id)
{
	if (!m_IsDefaultDevice)
		return;

	const EDataFlow expectedFlow = m_Flow;
	const ERole expectedRole = (expectedFlow == eCapture) ? eCommunications : eConsole;
	if (flow != expectedFlow || role != expectedRole)
		return;

	if (id) {
		if (m_DefaultDeviceId.compare(id) == 0)
			return;
		m_DefaultDeviceId = id;
	}
	else {
		if (m_DefaultDeviceId.empty())
			return;
		m_DefaultDeviceId.clear();
	}
	SetOffline(false);
	LOG_INFO("WASAPI: Default %s device changed", m_Tag.c_str());
	SetEvent(m_CaptureRestartEvent);
}

void WASAPICapture::SetOffline(bool isOffline) {
	m_IsOffline.store(isOffline);
}

bool WASAPICapture::IsCapturing() {
	return m_IsCapturing.load();
}

void WASAPICapture::ClearRecordedBytes()
{
	const std::lock_guard<std::mutex> lock(m_TaskWrapperImpl->m_Mutex);
	m_RecordedBytes.clear();
}

HRESULT WASAPICapture::ReconnectThreadLoop() {
	SetThreadDescription(
		GetCurrentThread(),
		L"WASAPICapture reconnect thread!");

	const HANDLE events[] = {
		m_ReconnectThreadStopEvent,
		m_CaptureReconnectEvent,
	};

	bool exit = false;
	while (!exit) {
		const DWORD ret = WaitForMultipleObjects(ARRAYSIZE(events), events,
							 false, INFINITE);
		switch (ret) {
			default:
			case WAIT_OBJECT_0: {
				exit = true;
				break;
			}
			case WAIT_OBJECT_0 + 1: {
				m_RetryWait.Wait();
				StartCapture();
				break;
			}
		}
	}
	return 0;
}
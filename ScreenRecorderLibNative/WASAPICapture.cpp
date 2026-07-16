//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#pragma comment(lib, "Mmdevapi.lib")

#include "Cleanup.h"
#include "WASAPICapture.h"
#include <mutex>
#include <ppltasks.h> 
#include "CoreAudio.util.h"
#include "DynamicWait.h"
#include "WASAPINotify.h"
#include "Exception.h"
#include <audioclientactivationparams.h>
#include "AudioActivationHandler.h"
#include<numeric>


using namespace std;

struct WASAPICapture::TaskWrapper {
	std::mutex m_Mutex;
	CComPtr<WASAPINotify> m_Notify;
	std::thread m_CaptureThread;
	std::thread m_ReconnectThread;
};

class AudioClientStopOnExit {
public:
	AudioClientStopOnExit(IAudioClient *p) : m_p(p) {}
	~AudioClientStopOnExit() {
		HRESULT hr = m_p->Stop();
		if (FAILED(hr)) {
			LOG_ERROR(L"IAudioClient::Stop failed: hr = 0x%08x", hr);
		}
	}

private:
	IAudioClient *m_p;
};

class AvRevertMmThreadCharacteristicsOnExit {
public:
	AvRevertMmThreadCharacteristicsOnExit(HANDLE hTask) : m_hTask(hTask) {}
	~AvRevertMmThreadCharacteristicsOnExit() {
		if (!AvRevertMmThreadCharacteristics(m_hTask)) {
			LOG_ERROR(L"AvRevertMmThreadCharacteristics failed: last error is %d", GetLastError());
		}
	}
private:
	HANDLE m_hTask;
};

WASAPICapture::WASAPICapture(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions, _In_ AUDIO_SOURCE *source) :
	m_DeviceName(L""),
	m_DeviceFriendlyName(L""),
	m_DefaultDeviceName(L""),
	m_Resampler(nullptr),
	m_pEnumerator(nullptr),
	m_Flow(eRender),
	m_Kind(AudioClientKind::Endpoint),
	m_IsDefaultDevice(false),
	m_AudioCaptureSource(nullptr),
	m_NeedSync(false)
{
	m_AudioCaptureSource = source;
	m_AudioOptions = audioOptions.get();
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
	LOG_DEBUG(L"Finalized WASAPICapture");
}

HRESULT WASAPICapture::Initialize(_In_ std::wstring deviceId, _In_ AudioClientKind kind) {
	m_Kind = kind;
	m_Flow = AudioClientKindToDeviceFlow(kind);
	AudioClientContext *pAudioClientContext = nullptr;
	HRESULT hr = InitializeAudioClient(deviceId, kind, &pAudioClientContext);
	if (SUCCEEDED(hr)) {
		m_AudioClientContext.reset(pAudioClientContext);
		WWMFResampler *pResampler;
		HRESULT resampleHr = InitializeResampler(m_AudioOptions->GetAudioSamplesPerSecond(), m_AudioOptions->GetAudioChannels(), pAudioClientContext, &m_InputFormat, &m_OutputFormat, &pResampler);
		if (SUCCEEDED(resampleHr)) {
			m_Resampler.reset(pResampler);
		}
	}
	return hr;
}

HRESULT ActivateAudioClientSync(
	const wchar_t *deviceId,
	const AUDIOCLIENT_ACTIVATION_PARAMS &params,
	IAudioClient **ppAudioClient)
{
	if (!ppAudioClient)
		return E_POINTER;
	*ppAudioClient = nullptr;

	// Package params into PROPVARIANT
	PROPVARIANT activateParams = {};
	activateParams.vt = VT_BLOB;
	activateParams.blob.cbSize = sizeof(params);
	activateParams.blob.pBlobData = (BYTE *)&params;

	Microsoft::WRL::ComPtr<AudioActivationHandler> audioActivationHandler;
	HRESULT hr = Microsoft::WRL::MakeAndInitialize<AudioActivationHandler>(&audioActivationHandler);
	if (FAILED(hr)) {
		return hr;
	}
	Microsoft::WRL::ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;

	hr = ActivateAudioInterfaceAsync(
		VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
	   __uuidof(IAudioClient),
	   &activateParams,
	   audioActivationHandler.Get(),
	   &asyncOp);

	if (FAILED(hr))
	{
		asyncOp.Reset();
		audioActivationHandler.Reset();
		return hr;
	}
	hr = audioActivationHandler->WaitAndGetResult(__uuidof(IAudioClient), reinterpret_cast<void **>(ppAudioClient));

	asyncOp.Reset();
	audioActivationHandler.Reset();
	return hr;
}

HRESULT WASAPICapture::InitializeAudioClient(
	_In_ std::wstring endpointID,
	_In_ AudioClientKind kind,
	_Outptr_ AudioClientContext **ppAudioClient)
{
	*ppAudioClient = nullptr;
	WAVEFORMATEX *pwfx;
	// activate an IAudioClient
	CComPtr<IAudioClient> pAudioClient = nullptr;
	HRESULT hr = E_FAIL;
	if (kind == AudioClientKind::ProcessLoopback) {
		if (!IsAudioClientActivationParamsAvailable()) {
			LOG_ERROR("Process loopback audio capture is not supported on this version of Windows");
			return E_FAIL;
		}
		DWORD processID;
		if (!TryParseDWORD(endpointID, processID)) {
			LOG_ERROR("Failed to parse process ID. It must be a valid number.");
			return E_FAIL;
		}
		m_DeviceFriendlyName = GetProcessNameFromPID(processID);

		AUDIOCLIENT_ACTIVATION_PARAMS audioclientActivationParams = {};
		audioclientActivationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
		audioclientActivationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
		audioclientActivationParams.ProcessLoopbackParams.TargetProcessId = processID;

		hr = ActivateAudioClientSync(L"", audioclientActivationParams, &pAudioClient);

		if (FAILED(hr)) {
			LOG_ERROR(L"ActivateAudioClientSync failed on %ls: hr = 0x%08x", GetDeviceFriendlyName().c_str(), hr);
			return hr;
		}
		RETURN_ON_BAD_HR(GetWaveFormat(kind, pAudioClient, true, &pwfx));
		CoTaskMemFreeOnExit freeMixFormat(pwfx);
		// Initialize the AudioClient in Shared Mode with the user specified buffer
		RETURN_ON_BAD_HR(hr = pAudioClient->Initialize(
			AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_LOOPBACK,
			AUDIO_CLIENT_BUFFER_100_NS,
			0,
			pwfx,
			nullptr));
	}
	else {
		CComPtr<IMMDevice> pDevice = nullptr;
		if (endpointID.empty() || m_IsDefaultDevice) {
			m_IsDefaultDevice = true;
			RETURN_ON_BAD_HR(GetDefaultAudioDevice(m_Flow, &pDevice));
		}
		else {
			RETURN_ON_BAD_HR(GetActiveAudioDevice(endpointID.c_str(), m_Flow, &pDevice));
		}

		if (pDevice) {
			LPWSTR deviceId;
			pDevice->GetId(&deviceId);
			m_DeviceName = std::wstring(deviceId);
			if (m_IsDefaultDevice) {
				m_DefaultDeviceName = m_DeviceName;
			}
			hr = GetAudioDeviceFriendlyName(deviceId, &m_DeviceFriendlyName);
			if (FAILED(hr)) {
				m_DeviceFriendlyName = L"Unknown Device";
			}
			CoTaskMemFree(deviceId);
		}
		else {
			LOG_ERROR("IMMDevice cannot be NULL");
			return E_FAIL;
		}
		if (pDevice == nullptr) {
			LOG_ERROR(L"IMMDevice is NULL");
			return E_FAIL;
		}
		hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&pAudioClient);
		if (FAILED(hr)) {
			LOG_ERROR(L"IMMDevice::Activate(IAudioClient) failed on %ls: hr = 0x%08x", GetDeviceFriendlyName().c_str(), hr);
			return hr;
		}


		DWORD streamFlags = 0;
		EDataFlow flow;
		GetAudioDeviceFlow(pDevice, &flow);
		switch (flow)
		{
			case eRender:
			{
				streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK;
				break;
			}
			case eCapture: {
				streamFlags = 0;
				break;
			}
			default: {
				streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK;
				break;
			}
		}
		RETURN_ON_BAD_HR(GetWaveFormat(kind, pAudioClient, true, &pwfx));
		CoTaskMemFreeOnExit freeMixFormat(pwfx);
		hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, AUDIO_CLIENT_BUFFER_100_NS, 0, pwfx, 0);
		if (FAILED(hr)) {
			LOG_ERROR(L"IAudioClient::Initialize failed on %ls: hr = 0x%08x", GetDeviceFriendlyName().c_str(), hr);
			return hr;
		}
	}

	auto audioClientContext = std::make_unique<AudioClientContext>(pAudioClient, kind);
	*ppAudioClient = audioClientContext.release();

	return hr;
}

HRESULT WASAPICapture::InitializeResampler(
	_In_ UINT32 samplerate,
	_In_ UINT32 nChannels,
	_In_ AudioClientContext *pAudioClientContext,
	_Out_ WWMFPcmFormat *audioInputFormat,
	_Out_ WWMFPcmFormat *audioOutputFormat,
	_Outptr_result_maybenull_ WWMFResampler **ppResampler)
{
	*ppResampler = nullptr;
	WWMFPcmFormat inputFormat = {};
	WWMFPcmFormat outputFormat = {};
	UINT32 outputSampleRate;

	WAVEFORMATEX *pwfx;
	RETURN_ON_BAD_HR(GetWaveFormat(pAudioClientContext->kind, pAudioClientContext->client, true, &pwfx));
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
		LOG_DEBUG("Resampler created for %ls", GetDeviceFriendlyName().c_str());
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
	_In_ AudioClientKind kind,
	_In_ CComPtr<IAudioClient> client,
	_In_ bool bInt16,
	_Out_ WAVEFORMATEX **pWaveFormat) {
	// get the default device format
	HRESULT hr = E_FAIL;
	WAVEFORMATEX *pwfx = nullptr;
	if (kind == AudioClientKind::ProcessLoopback) {

		pwfx = (WAVEFORMATEX *)CoTaskMemAlloc((sizeof(WAVEFORMATEX)));
		if (pwfx == nullptr) {
			return E_FAIL;
		}
		*pwfx = {};
		pwfx->wFormatTag = WAVE_FORMAT_PCM;
		pwfx->nChannels = 2;
		pwfx->nSamplesPerSec = 44100;
		pwfx->wBitsPerSample = 16;
		pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
		pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
		//pwfx = &wfx;
		hr = S_OK;
	}
	else {
		hr = client->GetMixFormat(&pwfx);
		if (FAILED(hr)) {
			LOG_ERROR(L"IAudioClient::GetMixFormat failed on %ls: hr = 0x%08x", GetDeviceFriendlyName().c_str(), hr);
			return hr;
		}

		if (bInt16) {
			// coerce int-16 wave format
			// can do this in-place since we're not changing the size of the format
			// also, the engine will auto-convert from float to int for us
			switch (pwfx->wFormatTag) {
				case WAVE_FORMAT_PCM:
				case WAVE_FORMAT_IEEE_FLOAT: {
					pwfx->wFormatTag = WAVE_FORMAT_PCM;
					pwfx->wBitsPerSample = 16;
					pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
					pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
					break;
				}
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
					break;
				}
				default: {
					LOG_ERROR(L"Don't know how to coerce WAVEFORMATEX with wFormatTag = 0x%08x to int-16", pwfx->wFormatTag);
					return E_UNEXPECTED;
				}
			}
		}
	}
	*pWaveFormat = pwfx;
	return hr;
}

HRESULT WASAPICapture::StartCaptureLoop(
		_In_ AudioClientContext *pAudioClientContext,
		_In_ HANDLE hStartedEvent,
		_In_ HANDLE hStopEvent,
		_In_ HANDLE hRestartEvent
) {
	HRESULT hr = E_FAIL;

	IAudioClient *pAudioClient = pAudioClientContext->client;

	UINT32 nBlockAlign = m_InputFormat.FrameBytes();
	UINT32 nFrames = 0;

	int bufferFrameCount = int(ceil(m_InputFormat.sampleRate * HundredNanosToSeconds(AUDIO_CLIENT_BUFFER_100_NS)));
	int bufferByteCount = bufferFrameCount * nBlockAlign;
	std::unique_ptr<BYTE[]> bufferData(new BYTE[bufferByteCount]{ 0 });
	{
		// activate an IAudioCaptureClient
		CComPtr<IAudioCaptureClient> pAudioCaptureClient = nullptr;
		hr = pAudioClient->GetService(
			__uuidof(IAudioCaptureClient),
			(void **)&pAudioCaptureClient
		);
		if (FAILED(hr)) {
			LOG_ERROR(L"IAudioClient::GetService(IAudioCaptureClient) failed on %ls: hr = 0x%08x", GetDeviceFriendlyName().c_str(), hr);
			return hr;
		}
		int lTimeBetweenFiresMillis = 0;

		if (pAudioClientContext->kind == AudioClientKind::ProcessLoopback) {
			lTimeBetweenFiresMillis = 5;
		}
		else {
			// get the default device periodicity
			REFERENCE_TIME hnsDefaultDevicePeriod;
			hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
			if (FAILED(hr)) {
				LOG_ERROR(L"IAudioClient::GetDevicePeriod failed on %ls: hr = 0x%08x", GetDeviceFriendlyName().c_str(), hr);
				return hr;
			}
			lTimeBetweenFiresMillis = static_cast<int>(HundredNanosToMillis((INT64)hnsDefaultDevicePeriod / 2));
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
		liFirstFire.QuadPart = -MillisToHundredNanos(static_cast<double>(lTimeBetweenFiresMillis)); // negative means relative time
		BOOL bOK = SetWaitableTimer(
			hWakeUp,
			&liFirstFire,
			lTimeBetweenFiresMillis,
			NULL, NULL, FALSE
		);
		if (!bOK) {
			DWORD dwErr = GetLastError();
			LOG_ERROR(L"SetWaitableTimer failed on %ls: last error = %u", GetDeviceFriendlyName().c_str(), dwErr);
			return HRESULT_FROM_WIN32(dwErr);
		}
		CancelWaitableTimerOnExit cancelWakeUp(hWakeUp);

		// call IAudioClient::Start
		hr = pAudioClient->Start();
		if (FAILED(hr)) {
			LOG_ERROR(L"IAudioClient::Start failed on %ls: hr = 0x%08x", GetDeviceFriendlyName().c_str(), hr);
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
				UINT64 nQpcPosition;
				hr = pAudioCaptureClient->GetBuffer(
					&pData,
					&nNumFramesToRead,
					&dwFlags,
					&nDevicePosition,
					&nQpcPosition
				);

				if (FAILED(hr)) {
					LOG_ERROR(L"IAudioCaptureClient::GetBuffer failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, GetDeviceFriendlyName().c_str(), hr);
					bDone = true;
					continue; // exits loop
				}

				if (m_IsPaused.load()) {
					hr = pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
					if (FAILED(hr)) {
						LOG_ERROR(L"IAudioCaptureClient::ReleaseBuffer failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, GetDeviceFriendlyName().c_str(), hr);
						bDone = true;
					}
					continue;
				}

				bool isDiscontinuity = false;
				if ((dwFlags & (AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)) != 0) {
					if (bFirstPacket) {
						LOG_DEBUG(L"Probably spurious glitch reported on first packet on %ls", GetDeviceFriendlyName().c_str());
					}
					else {
						LOG_DEBUG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames on %ls", dwFlags, nPasses, nFrames, GetDeviceFriendlyName().c_str());
						isDiscontinuity = true;
					}
				}
				else if ((dwFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
					//Captured data should be replaced with silence as according to https://docs.microsoft.com/en-us/windows/win32/coreaudio/capturing-a-stream
					LOG_DEBUG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames on %ls", dwFlags, nPasses, nFrames, GetDeviceFriendlyName().c_str());
					memset(pData, 0, sizeof(pData));
				}
				else if (0 != dwFlags) {
					LOG_DEBUG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames on %ls", dwFlags, nPasses, nFrames, GetDeviceFriendlyName().c_str());
				}

				if (0 == nNumFramesToRead) {
					LOG_ERROR(L"IAudioCaptureClient::GetBuffer said to read 0 frames on pass %u after %u frames on %ls", nPasses, nFrames, GetDeviceFriendlyName().c_str());
					hr = E_UNEXPECTED;
					pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
					bDone = true;
					continue; // exits loop
				}

				UINT32 size = nNumFramesToRead * nBlockAlign;
				memcpy_s(bufferData.get(), bufferByteCount, pData, size);

				hr = pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
				if (FAILED(hr)) {
					LOG_ERROR(L"IAudioCaptureClient::ReleaseBuffer failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, GetDeviceFriendlyName().c_str(), hr);
					bDone = true;
					continue; // exits loop
				}
				std::vector<BYTE> recordedBytes(&bufferData[0], &bufferData[size]);
				//This should reduce glitching if there is discontinuity in the audio stream.
				if (isDiscontinuity) {
					UINT64 expectedPosition = nLastDevicePosition + nNumFramesToRead;
					if (nDevicePosition > expectedPosition)
					{
						INT64 frameDiff = max(0, static_cast<INT64>(nDevicePosition) - static_cast<INT64>(expectedPosition));
						recordedBytes.insert(recordedBytes.begin(), (size_t)(frameDiff * nBlockAlign), 0);
						LOG_DEBUG(L"Discontinuity detected, padded audio bytes with %d bytes of silence on %ls", frameDiff, GetDeviceFriendlyName().c_str());
					}
				}
#pragma warning(disable: 26110)
				const std::scoped_lock lock(m_TaskWrapperImpl->m_Mutex, StaticMutex);
#pragma prefast(suppress: __WARNING_INCORRECT_ANNOTATION, "IAudioCaptureClient::GetBuffer SAL annotation implies a 1-byte buffer")

				AudioPacket packet = AudioPacket(recordedBytes, nNumFramesToRead, nQpcPosition);
				m_RecordedAudioPackets.push_back(packet);
				nFrames += nNumFramesToRead;
				bFirstPacket = false;
				nLastDevicePosition = nDevicePosition;
			}

			if (FAILED(hr)) {
				LOG_ERROR(L"IAudioCaptureClient::GetNextPacketSize failed on pass %u after %u frames on %ls: hr = 0x%08x", nPasses, nFrames, GetDeviceFriendlyName().c_str(), hr);
				bDone = true;
				continue; // exits loop
			}

			dwWaitResult = WaitForMultipleObjects(ARRAYSIZE(waitArray), waitArray, FALSE, 5000);

			if (WAIT_OBJECT_0 == dwWaitResult) {
				LOG_DEBUG(L"Received stop event after %u passes and %u frames on %ls", nPasses, nFrames, GetDeviceFriendlyName().c_str());
				bDone = true;
			}
			else if (WAIT_OBJECT_0 + 1 == dwWaitResult) {
				LOG_DEBUG(L"Received restart event after %u passes and %u frames on %ls", nPasses, nFrames, GetDeviceFriendlyName().c_str());
				bDone = true;
			}
			else if (WAIT_TIMEOUT == dwWaitResult) {
				LOG_ERROR(L"WaitForMultipleObjects timeout on pass %u after %u frames on %ls", dwWaitResult, nPasses, nFrames, GetDeviceFriendlyName().c_str());
				hr = E_UNEXPECTED;
				bDone = true;
			}
			else if (WAIT_OBJECT_0 + 2 != dwWaitResult) {
				LOG_ERROR(L"Unexpected WaitForMultipleObjects return value %u on pass %u after %u frames on %ls", dwWaitResult, nPasses, nFrames, GetDeviceFriendlyName().c_str());
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
	return vector < BYTE>();
}
int WASAPICapture::GetNextFrameCount()
{
	int availableFrameCount = std::accumulate(m_RecordedAudioPackets.begin(), m_RecordedAudioPackets.end(), 0, [this](int a, AudioPacket b) {
		return (a + b.data.size() / m_InputFormat.FrameBytes());
	});
	return availableFrameCount;
}
std::vector<BYTE> WASAPICapture::GetRecordedBytesByDuration(UINT64 duration100Nanos, _Out_ UINT64 *qpcTimestamp)
{
	int frameCount = int(ceil(m_InputFormat.sampleRate * HundredNanosToSeconds(duration100Nanos)));
	return GetRecordedBytesByFrameCount(frameCount, qpcTimestamp);
}
std::vector<BYTE> WASAPICapture::GetRecordedBytesByFrameCount(int requestedFrameCount, _Out_ UINT64 *qpcTimestamp)
{
	std::vector<BYTE> newvector;
	*qpcTimestamp = 0;
	int recordedFrameCount{};
	if (m_RecordedAudioPackets.size() > 0)
	{
		const std::lock_guard<std::mutex> lock(m_TaskWrapperImpl->m_Mutex);
		size_t remainingBytes = 0;
		int readPacketCount = 0;
		for each (AudioPacket recordedPacket in m_RecordedAudioPackets)
		{
			if (recordedFrameCount < requestedFrameCount) {
				newvector.insert(newvector.end(), recordedPacket.data.begin(), recordedPacket.data.end());
				recordedFrameCount += recordedPacket.frameCount;
				if (readPacketCount == 0) {
					*qpcTimestamp = recordedPacket.timestamp100ns;
				}
				readPacketCount++;
			}
			else {
				remainingBytes += recordedPacket.data.size();
			}
		}
		int diff = requestedFrameCount - recordedFrameCount;
		if (m_NeedSync) {
			if (diff > 0) {
				newvector.insert(newvector.begin(), diff * m_InputFormat.FrameBytes(), 0);
				LOG_TRACE("Padded packet with %d bytes on WASAPICapture %ls", diff, GetDeviceFriendlyName().c_str());
			}
			m_NeedSync = false;
		}
		m_RecordedAudioPackets.erase(m_RecordedAudioPackets.begin(), m_RecordedAudioPackets.begin() + readPacketCount);
		LOG_TRACE(L"Got %d bytes from WASAPICapture %ls. %d bytes remaining.", newvector.size(), GetDeviceFriendlyName().c_str(), remainingBytes);

		// convert audio
		if (m_Resampler && newvector.size() > 0) {
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
	if (!m_AudioClientContext) {
		HRESULT hr = Initialize(m_DeviceName, m_Kind);
		if (FAILED(hr)) {
			if (hr == E_NOTFOUND) {
				SetOffline(true);
			}
			return hr;
		}
		m_RecordedAudioPackets.clear();
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
		CoUninitializeOnExit coUninitialize;
		_set_se_translator(ExceptionTranslator);
		// register with MMCSS
		DWORD nTaskIndex = 0;
		HANDLE hTask = AvSetMmThreadCharacteristics(L"Audio", &nTaskIndex);
		if (hTask != NULL) {
			AvSetMmThreadPriority(hTask, AVRT_PRIORITY_HIGH);
		}
		if (NULL == hTask) {
			DWORD dwErr = GetLastError();
			LOG_ERROR(L"AvSetMmThreadCharacteristics failed on %ls: last error = %u", GetDeviceFriendlyName().c_str(), dwErr);
			return HRESULT_FROM_WIN32(dwErr);
		}
		AvRevertMmThreadCharacteristicsOnExit unregisterMmcss(hTask);
		try {
			if (SUCCEEDED(hr)) {
				hr = StartCaptureLoop(m_AudioClientContext.get(), m_CaptureStartedEvent, m_CaptureStopEvent, m_CaptureRestartEvent);
			}
			if (FAILED(hr)) {
				LOG_ERROR(L"Audio capture loop failed to start: hr = 0x%08x", hr);
			}
		}
		catch (const AccessViolationException &) {
			hr = EXCEPTION_ACCESS_VIOLATION;
			LOG_ERROR(L"Exception in WASAPICapture: AccessViolationException");
		}
		catch (...) {
			hr = E_UNEXPECTED;
			LOG_ERROR(L"Exception in WASAPICapture");
		}
		m_IsCapturing.store(false);
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
			m_AudioClientContext.reset();
			m_NeedSync = true;
		}
		else {
			return S_FALSE;
		}
	}
	catch (const std::system_error &e) {
		LOG_ERROR(L"Exception in StopCapture: %s", s2ws(e.what()).c_str());
	}
	return S_OK;
}

HRESULT WASAPICapture::PauseCapture()
{
	m_IsPaused.store(true);
	return S_OK;
}
HRESULT WASAPICapture::ResumeCapture()
{
	m_IsPaused.store(false);
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
	catch (const std::system_error &e) {
		LOG_ERROR(L"Exception in StopReconnectThread: %s", s2ws(e.what()).c_str());
	}
	return S_OK;
}

bool WASAPICapture::StartListeners() {
	if (!m_IsRegisteredForEndpointNotifications)
	{
		// Create the device enumerator
		IMMDeviceEnumerator *pEnumerator = nullptr;
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


void WASAPICapture::SetDefaultDevice(EDataFlow flow, ERole role, LPCWSTR id)
{
	if (!m_IsDefaultDevice)
		return;

	const EDataFlow expectedFlow = m_Flow;
	const ERole expectedRole = (expectedFlow == eCapture) ? eCommunications : eConsole;
	if (flow != expectedFlow || role != expectedRole)
		return;

	if (id) {
		if (m_DefaultDeviceName.compare(id) == 0)
			return;
		m_DefaultDeviceName = id;
	}
	else {
		if (m_DefaultDeviceName.empty())
			return;
		m_DefaultDeviceName.clear();
	}
	SetOffline(false);
	LOG_INFO("WASAPI: Default %s device changed", GetDeviceFriendlyName().c_str());
	SetEvent(m_CaptureRestartEvent);
}

void WASAPICapture::SetOffline(bool isOffline) {
	m_IsOffline.store(isOffline);
}

bool WASAPICapture::IsCapturing() {
	return m_IsCapturing.load();
}

bool WASAPICapture::IsPaused() {
	return m_IsPaused.load();
}

void WASAPICapture::ClearRecordedBytes()
{
	const std::lock_guard<std::mutex> lock(m_TaskWrapperImpl->m_Mutex);
	m_RecordedAudioPackets.clear();
}

HRESULT WASAPICapture::ReconnectThreadLoop() {
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
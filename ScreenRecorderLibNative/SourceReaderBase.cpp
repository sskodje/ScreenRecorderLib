#include "SourceReaderBase.h"
#include <Mferror.h>
#include "Cleanup.h"
using namespace std;

SourceReaderBase::SourceReaderBase() :
	m_Sample{ nullptr },
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_LastSampleReceivedTimeStamp{ 0 },
	m_LastGrabTimeStamp{ 0 },
	m_ReferenceCount(1),
	m_Stride(0),
	m_FrameRate(0),
	m_FrameSize{},
	m_FramerateTimer(nullptr),
	m_NewFrameEvent(nullptr),
	m_StopCaptureEvent(nullptr),
	m_OutputMediaType(nullptr),
	m_InputMediaType(nullptr),
	m_SourceReader(nullptr),
	m_MediaTransform(nullptr),
	m_TextureManager(nullptr),
	m_BufferSize(0),
	m_PtrFrameBuffer(nullptr),
	m_RecordingSource(nullptr),
	m_DeviceManager(nullptr),
	m_ResetToken(0)
{
	InitializeCriticalSection(&m_CriticalSection);
	m_NewFrameEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	m_StopCaptureEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}
SourceReaderBase::~SourceReaderBase()
{
	Close();
	EnterCriticalSection(&m_CriticalSection);
	SafeRelease(&m_Device);
	SafeRelease(&m_DeviceContext);

	delete m_FramerateTimer;
	CloseHandle(m_NewFrameEvent);
	CloseHandle(m_StopCaptureEvent);
	LeaveCriticalSection(&m_CriticalSection);
	DeleteCriticalSection(&m_CriticalSection);

	delete[] m_PtrFrameBuffer;
	m_PtrFrameBuffer = nullptr;
}

HRESULT SourceReaderBase::StartCapture(_In_ RECORDING_SOURCE_BASE &recordingSource)
{
	HRESULT hr;
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveCriticalSection(&m_CriticalSection, L"StartCapture");
	m_RecordingSource = &recordingSource;
	long streamIndex;

	if (recordingSource.SourceStream) {
		RETURN_ON_BAD_HR(hr = InitializeSourceReader(recordingSource.SourceStream, recordingSource.CaptureFormatIndex, &streamIndex, &m_SourceReader, &m_InputMediaType, &m_OutputMediaType, &m_MediaTransform));
	}
	else {
		RETURN_ON_BAD_HR(hr = InitializeSourceReader(recordingSource.SourcePath, recordingSource.CaptureFormatIndex, &streamIndex, &m_SourceReader, &m_InputMediaType, &m_OutputMediaType, &m_MediaTransform));
	}

	RETURN_ON_BAD_HR(GetDefaultStride(m_OutputMediaType, &m_Stride));
	RETURN_ON_BAD_HR(GetFrameRate(m_InputMediaType, &m_FrameRate));
	RETURN_ON_BAD_HR(GetFrameSize(m_InputMediaType, &m_FrameSize));
	if (SUCCEEDED(hr))
	{
		ResetEvent(m_StopCaptureEvent);
		// Ask for the first sample.
		hr = m_SourceReader->ReadSample(streamIndex, 0, NULL, NULL, NULL, NULL);
	}
	return hr;
}

HRESULT SourceReaderBase::GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *nativeMediaSize)
{
	if (!m_InputMediaType) {
		MeasureExecutionTime measure(L"SourceReaderBase GetNativeSize");
		long streamIndex;
		RETURN_ON_BAD_HR(MFStartup(MF_VERSION, MFSTARTUP_LITE));
		CComPtr<IMFMediaType> pInputMediaType;
		if (recordingSource.SourceStream) {
			RETURN_ON_BAD_HR(InitializeSourceReader(recordingSource.SourceStream, recordingSource.CaptureFormatIndex, &streamIndex, &m_SourceReader, &pInputMediaType, nullptr, nullptr));
		}
		else {
			RETURN_ON_BAD_HR(InitializeSourceReader(recordingSource.SourcePath, recordingSource.CaptureFormatIndex, &streamIndex, &m_SourceReader, &pInputMediaType, nullptr, nullptr));
		}
		RETURN_ON_BAD_HR(MFShutdown());
		return GetFrameSize(pInputMediaType, nativeMediaSize);
	}
	else {
		return GetFrameSize(m_InputMediaType, nativeMediaSize);
	}
}

void SourceReaderBase::Close()
{
	EnterCriticalSection(&m_CriticalSection);
	SafeRelease(&m_Sample);
	if (m_FramerateTimer) {
		m_FramerateTimer->StopTimer(true);
	}
	SetEvent(m_StopCaptureEvent);
	LeaveCriticalSection(&m_CriticalSection);
	SafeRelease(&m_SourceReader);
	SafeRelease(&m_InputMediaType);
	SafeRelease(&m_MediaTransform);
}

HRESULT SourceReaderBase::AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame)
{
	DWORD result = WAIT_OBJECT_0;

	if (m_LastGrabTimeStamp.QuadPart >= m_LastSampleReceivedTimeStamp.QuadPart) {
		result = WaitForSingleObject(m_NewFrameEvent, timeoutMillis);
	}
	HRESULT hr = S_OK;
	if (result == WAIT_OBJECT_0) {
		//Only create frame if the caller accepts one.
		if (ppFrame) {
			EnterCriticalSection(&m_CriticalSection);
			LeaveCriticalSectionOnExit leaveCriticalSection(&m_CriticalSection, L"GetFrameBuffer");


			DWORD len;
			BYTE *data;
			hr = m_Sample->Lock(&data, NULL, &len);
			ExecuteFuncOnExit releaseSampleLock([&]() {
				m_Sample->Unlock();
			});
			if (FAILED(hr))
			{
				delete[] m_PtrFrameBuffer;
				m_PtrFrameBuffer = nullptr;
				return hr;
			}
			if (SUCCEEDED(hr)) {
				hr = ResizeFrameBuffer(len);
				int bytesPerPixel = abs(m_Stride) / m_FrameSize.cx;
				//Copy the bitmap buffer, with handling of negative stride. https://docs.microsoft.com/en-us/windows/win32/medfound/image-stride
				hr = MFCopyImage(
					m_PtrFrameBuffer,       // Destination buffer.
					abs(m_Stride),                    // Destination stride. We use the absolute value to flip bitmaps with negative stride. 
					m_Stride > 0 ? data : data + (m_FrameSize.cy - 1) * abs(m_Stride), // First row in source image with positive stride, or the last row with negative stride.
					m_Stride,						  // Source stride.
					bytesPerPixel * m_FrameSize.cx,	      // Image width in bytes.
					m_FrameSize.cy						  // Image height in pixels.
				);

				CComPtr<ID3D11Texture2D> pTexture;
				hr = m_TextureManager->CreateTextureFromBuffer(m_PtrFrameBuffer, m_Stride, m_FrameSize.cx, m_FrameSize.cy, &pTexture, 0, D3D11_BIND_SHADER_RESOURCE);
				if (SUCCEEDED(hr)) {
					*ppFrame = pTexture;
					(*ppFrame)->AddRef();
					QueryPerformanceCounter(&m_LastGrabTimeStamp);
				}
				if (m_RecordingSource->RecordingNewFrameDataCallback) {
					m_RecordingSource->RecordingNewFrameDataCallback(abs(m_Stride), m_PtrFrameBuffer, len, m_FrameSize.cx, m_FrameSize.cy);
				}
			}
		}
	}
	else if (result == WAIT_TIMEOUT) {
		hr = DXGI_ERROR_WAIT_TIMEOUT;
	}
	else {
		DWORD dwErr = GetLastError();
		LOG_ERROR(L"WaitForSingleObject failed: last error = %u", dwErr);
		hr = HRESULT_FROM_WIN32(dwErr);
	}
	return hr;
}

HRESULT SourceReaderBase::ResizeFrameBuffer(UINT bufferSize) {
	// Old buffer too small
	if (bufferSize > m_BufferSize)
	{
		if (m_PtrFrameBuffer)
		{
			delete[] m_PtrFrameBuffer;
			m_PtrFrameBuffer = nullptr;
		}
		m_PtrFrameBuffer = new (std::nothrow) BYTE[bufferSize];
		if (!m_PtrFrameBuffer)
		{
			m_BufferSize = 0;
			LOG_ERROR(L"Failed to allocate memory for frame");
			return E_OUTOFMEMORY;
		}

		// Update buffer size
		m_BufferSize = bufferSize;
	}
	return S_OK;
}

HRESULT SourceReaderBase::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_Device->AddRef();
	m_DeviceContext->AddRef();

	m_TextureManager = make_unique<TextureManager>();
	m_TextureManager->Initialize(m_DeviceContext, m_Device);

	if (m_MediaTransform) {
		m_MediaTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
	}
	if (!m_DeviceManager) {
		RETURN_ON_BAD_HR(MFCreateDXGIDeviceManager(&m_ResetToken, &m_DeviceManager));
	}
	RETURN_ON_BAD_HR(m_DeviceManager->ResetDevice(pDevice, m_ResetToken));
	return S_OK;
}
HRESULT SourceReaderBase::WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect)
{
	CComPtr<ID3D11Texture2D> pProcessedTexture;
	HRESULT hr = AcquireNextFrame(timeoutMillis, &pProcessedTexture);
	RETURN_ON_BAD_HR(hr);

	D3D11_TEXTURE2D_DESC frameDesc;
	pProcessedTexture->GetDesc(&frameDesc);
	RECORDING_SOURCE *recordingSource = dynamic_cast<RECORDING_SOURCE *>(m_RecordingSource);
	if (recordingSource && recordingSource->SourceRect.has_value()
		&& IsValidRect(recordingSource->SourceRect.value())
		&& (RectWidth(recordingSource->SourceRect.value()) != frameDesc.Width || (RectHeight(recordingSource->SourceRect.value()) != frameDesc.Height))) {
		ID3D11Texture2D *pCroppedTexture;
		RETURN_ON_BAD_HR(hr = m_TextureManager->CropTexture(pProcessedTexture, recordingSource->SourceRect.value(), &pCroppedTexture));
		if (hr == S_OK) {
			pProcessedTexture.Release();
			pProcessedTexture.Attach(pCroppedTexture);
		}
	}
	pProcessedTexture->GetDesc(&frameDesc);

	RECT contentRect = destinationRect;
	if (RectWidth(destinationRect) != frameDesc.Width || RectHeight(destinationRect) != frameDesc.Height) {
		ID3D11Texture2D *pResizedTexture;
		RETURN_ON_BAD_HR(hr = m_TextureManager->ResizeTexture(pProcessedTexture, SIZE{ RectWidth(destinationRect),RectHeight(destinationRect) }, m_RecordingSource->Stretch, &pResizedTexture, &contentRect));
		pProcessedTexture.Release();
		pProcessedTexture.Attach(pResizedTexture);
	}

	pProcessedTexture->GetDesc(&frameDesc);

	SIZE contentOffset = GetContentOffset(m_RecordingSource->Anchor, destinationRect, contentRect);
	long left = destinationRect.left + offsetX + contentOffset.cx;
	long top = destinationRect.top + offsetY + contentOffset.cy;
	long right = left + MakeEven(frameDesc.Width);
	long bottom = top + MakeEven(frameDesc.Height);

	m_TextureManager->DrawTexture(pSharedSurf, pProcessedTexture, RECT{ left,top,right,bottom });
	return hr;
}

//From IUnknown 
STDMETHODIMP SourceReaderBase::QueryInterface(REFIID riid, void **ppvObject)
{
	static const QITAB qit[] = { QITABENT(SourceReaderBase, IMFSourceReaderCallback),{ 0 }, };
	return QISearch(this, qit, riid, ppvObject);
}
//From IUnknown
ULONG SourceReaderBase::Release()
{
	ULONG count = InterlockedDecrement(&m_ReferenceCount);
	if (count == 0)
		delete this;
	// For thread safety
	return count;
}
//From IUnknown
ULONG SourceReaderBase::AddRef()
{
	return InterlockedIncrement(&m_ReferenceCount);
}

HRESULT SourceReaderBase::CreateOutputMediaType(_In_ SIZE frameSize, _Outptr_ IMFMediaType **pType, _Out_ LONG *stride)
{
	*pType = nullptr;
	HRESULT hr;
	CComPtr<IMFMediaType> pOutputMediaType = nullptr;
	//DSP output MediaType
	RETURN_ON_BAD_HR(hr = MFCreateMediaType(&pOutputMediaType));
	RETURN_ON_BAD_HR(hr = pOutputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	RETURN_ON_BAD_HR(hr = pOutputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32));
	RETURN_ON_BAD_HR(hr = pOutputMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	RETURN_ON_BAD_HR(hr = MFSetAttributeSize(pOutputMediaType, MF_MT_FRAME_SIZE, frameSize.cx, frameSize.cy));
	RETURN_ON_BAD_HR(hr = MFSetAttributeRatio(pOutputMediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
	GetDefaultStride(pOutputMediaType, stride);
	if (pType) {
		*pType = pOutputMediaType;
		(*pType)->AddRef();
	}
	return hr;
}

HRESULT SourceReaderBase::CreateIMFTransform(_In_ DWORD streamIndex, _In_ IMFMediaType *pInputMediaType, _Outptr_ IMFTransform **ppTransform, _Outptr_ IMFMediaType **ppOutputMediaType)
{
	if (ppTransform) {
		*ppTransform = nullptr;
	}
	if (ppOutputMediaType) {
		*ppOutputMediaType = nullptr;
	}
	CComPtr<IMFTransform> pConverter = nullptr;
	CComPtr<IMFMediaType> pOutputMediaType = nullptr;
	HRESULT hr;

	UINT32 width;
	UINT32 height;
	//Get width and height
	RETURN_ON_BAD_HR(hr = MFGetAttributeSize(pInputMediaType, MF_MT_FRAME_SIZE, &width, &height));

	LONG stride;
	CreateOutputMediaType(SIZE{ (LONG)width,(LONG)height }, &pOutputMediaType, &stride);

	RETURN_ON_BAD_HR(hr = CoCreateInstance(CLSID_VideoProcessorMFT, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pConverter)));
	RETURN_ON_BAD_HR(hr = pConverter->SetInputType(streamIndex, pInputMediaType, 0));
	GUID outputVideoFormat;
	pOutputMediaType->GetGUID(MF_MT_SUBTYPE, &outputVideoFormat);

	GUID guidMinor;
	GUID guidMajor;
	for (int i = 0;; i++)
	{
		IMFMediaType *mediaType;
		hr = pConverter->GetOutputAvailableType(streamIndex, i, &mediaType);
		if (FAILED(hr))
		{
			break;
		}
		hr = mediaType->GetGUID(MF_MT_MAJOR_TYPE, &guidMajor);
		hr = mediaType->GetGUID(MF_MT_SUBTYPE, &guidMinor);
		if (guidMinor == outputVideoFormat) {
			break;
		}

		mediaType->Release();
	}

	RETURN_ON_BAD_HR(hr = pConverter->SetOutputType(streamIndex, pOutputMediaType, 0));
	if (ppTransform) {
		*ppTransform = pConverter;
		(*ppTransform)->AddRef();
	}
	if (ppOutputMediaType) {
		*ppOutputMediaType = pOutputMediaType;
		(*ppOutputMediaType)->AddRef();
	}
	return hr;
}

//Method from IMFSourceReaderCallback
HRESULT SourceReaderBase::OnReadSample(HRESULT status, DWORD streamIndex, DWORD streamFlags, LONGLONG timeStamp, IMFSample *sample)
{
	HRESULT hr = status;
	if (SUCCEEDED(hr) && WaitForSingleObject(m_StopCaptureEvent, 0) != WAIT_OBJECT_0) {
		if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
			PROPVARIANT var;
			HRESULT hr = InitPropVariantFromInt64(0, &var);
			hr = m_SourceReader->SetCurrentPosition(GUID_NULL, var);
			PropVariantClear(&var);
		}
		if (sample)
		{
			EnterCriticalSection(&m_CriticalSection);
			{
				LeaveCriticalSectionOnExit leaveCriticalSection(&m_CriticalSection, L"OnReadSample");
				if (m_MediaTransform) {
					//Run media transform to convert sample to MFVideoFormat_ARGB32
					MFT_OUTPUT_STREAM_INFO info{};
					hr = m_MediaTransform->GetOutputStreamInfo(0, &info);
					if (FAILED(hr)) {
						LOG_ERROR(L"GetOutputStreamInfo failed: hr = 0x%08x", hr);
					}
					bool transformProvidesSamples = info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES);
					MFT_OUTPUT_DATA_BUFFER outputDataBuffer;
					RtlZeroMemory(&outputDataBuffer, sizeof(outputDataBuffer));
					outputDataBuffer.dwStreamID = 0;
					if (!transformProvidesSamples) {
						IMFMediaBuffer *transformBuffer = nullptr;
						//create a buffer for the output sample
						hr = MFCreateMemoryBuffer(info.cbSize, &transformBuffer);
						if (FAILED(hr))
						{
							LOG_ERROR(L"MFCreateMemoryBuffer failed: hr = 0x%08x", hr);
						}
						IMFSample *transformSample;
						hr = MFCreateSample(&transformSample);
						if (FAILED(hr)) {
							LOG_ERROR(L"MFCreateSample failed: hr = 0x%08x", hr);
						}
						hr = transformSample->AddBuffer(transformBuffer);

						outputDataBuffer.pSample = transformSample;
						SafeRelease(&transformBuffer);
					}
					hr = m_MediaTransform->ProcessInput(0, sample, 0);
					if (FAILED(hr)) {
						LOG_ERROR(L"ProcessInput failed: hr = 0x%08x", hr);
					}
					DWORD dwDSPStatus = 0;
					hr = m_MediaTransform->ProcessOutput(0, 1, &outputDataBuffer, &dwDSPStatus);
					if (FAILED(hr)) {
						LOG_ERROR(L"ProcessOutput failed: hr = 0x%08x", hr);
					}
					SafeRelease(&m_Sample);
					//Store the converted media buffer
					IMFMediaBuffer *mediaBuffer = NULL;
					outputDataBuffer.pSample->GetBufferByIndex(0, &mediaBuffer);
					outputDataBuffer.pSample->Release();
					m_Sample = mediaBuffer;
				}
				else {
					IMFMediaBuffer *mediaBuffer = NULL;
					sample->GetBufferByIndex(0, &mediaBuffer);
					sample->Release();
					m_Sample = mediaBuffer;
				}
				//Update timestamp and notify that there is a new sample available
				QueryPerformanceCounter(&m_LastSampleReceivedTimeStamp);
				SetEvent(m_NewFrameEvent);
			}
			if (SUCCEEDED(hr)) {
				if (!m_FramerateTimer) {
					m_FramerateTimer = new HighresTimer();
					m_FramerateTimer->StartRecurringTimer((INT64)floor(1000 / m_FrameRate));
				}
				if (m_FrameRate > 0) {
					auto t1 = std::chrono::high_resolution_clock::now();
					auto sleepTime = m_FramerateTimer->GetMillisUntilNextTick();
					MeasureExecutionTime measureNextTick(L"OnReadSample scheduled delay");
					hr = m_FramerateTimer->WaitForNextTick();
					if (SUCCEEDED(hr)) {
						auto t2 = std::chrono::high_resolution_clock::now();
						std::chrono::duration<double, std::milli> ms_double = t2 - t1;
						double diff = ms_double.count() - sleepTime;
						measureNextTick.SetName(string_format(L"OnReadSample scheduled delay for %.2f ms for next frame. Actual delay differed by: %.2f ms, with a total delay of", sleepTime, diff));
					}
				}
			}
		}
		// Request the next frame.
		if (m_SourceReader && (SUCCEEDED(hr))) {
			hr = m_SourceReader->ReadSample(streamIndex, 0, NULL, NULL, NULL, NULL);
			if (SUCCEEDED(hr)) {
				return hr;
			}
		}
	}
	return hr;
}
//Method from IMFSourceReaderCallback 
STDMETHODIMP SourceReaderBase::OnEvent(DWORD, IMFMediaEvent *) { return S_OK; }
//Method from IMFSourceReaderCallback 
STDMETHODIMP SourceReaderBase::OnFlush(DWORD) { return S_OK; }
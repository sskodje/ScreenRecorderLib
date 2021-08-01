#include "SourceReaderBase.h"
#include <Mferror.h>
#include "Cleanup.h"

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
	m_OutputMediaType(nullptr),
	m_InputMediaType(nullptr),
	m_SourceReader(nullptr),
	m_MediaTransform(nullptr)
{
	InitializeCriticalSection(&m_CriticalSection);
	m_NewFrameEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}
SourceReaderBase::~SourceReaderBase()
{
	Close();
	EnterCriticalSection(&m_CriticalSection);
	SafeRelease(&m_Device);
	SafeRelease(&m_DeviceContext);

	delete m_FramerateTimer;
	CloseHandle(m_NewFrameEvent);
	LeaveCriticalSection(&m_CriticalSection);
	DeleteCriticalSection(&m_CriticalSection);
}

HRESULT SourceReaderBase::StartCapture(_In_ std::wstring source)
{
	HRESULT hr;
	long streamIndex;
	RETURN_ON_BAD_HR(hr = InitializeSourceReader(source, &streamIndex, &m_SourceReader, &m_InputMediaType, &m_OutputMediaType, &m_MediaTransform));
	RETURN_ON_BAD_HR(GetDefaultStride(m_OutputMediaType, &m_Stride));
	RETURN_ON_BAD_HR(GetFrameRate(m_InputMediaType, &m_FrameRate));
	RETURN_ON_BAD_HR(GetFrameSize(m_InputMediaType, &m_FrameSize));
	if (SUCCEEDED(hr))
	{
		// Ask for the first sample.
		hr = m_SourceReader->ReadSample(streamIndex, 0, NULL, NULL, NULL, NULL);
	}
	return hr;
}

void SourceReaderBase::Close()
{
	EnterCriticalSection(&m_CriticalSection);
	SafeRelease(&m_Sample);
	if (m_FramerateTimer) {
		LOG_DEBUG("Stopping source reader sync timer");
		m_FramerateTimer->StopTimer(true);
	}
	SafeRelease(&m_SourceReader);
	SafeRelease(&m_InputMediaType);
	SafeRelease(&m_MediaTransform);
	LeaveCriticalSection(&m_CriticalSection);
	LOG_DEBUG("Closed source reader");
}

HRESULT SourceReaderBase::GetFrame(_Inout_ FRAME_INFO *pFrameInfo, _In_ int timeoutMs)
{
	DWORD result = WaitForSingleObject(m_NewFrameEvent, timeoutMs);
	HRESULT hr;
	if (result == WAIT_OBJECT_0) {
		EnterCriticalSection(&m_CriticalSection);
		LeaveCriticalSectionOnExit leaveCriticalSection(&m_CriticalSection, L"GetFrameBuffer");
		DWORD len;
		BYTE *data;
		hr = m_Sample->Lock(&data, NULL, &len);
		if (FAILED(hr))
		{
			delete[] pFrameInfo->PtrFrameBuffer;
			pFrameInfo->PtrFrameBuffer = nullptr;
		}
		if (SUCCEEDED(hr)) {
			hr = ResizeFrameBuffer(pFrameInfo, len);
		}
		if (SUCCEEDED(hr)) {
			QueryPerformanceCounter(&m_LastGrabTimeStamp);
			int bytesPerPixel = abs(m_Stride) / m_FrameSize.cx;
			//Copy the bitmap buffer, with handling of negative stride. https://docs.microsoft.com/en-us/windows/win32/medfound/image-stride
			hr = MFCopyImage(
				pFrameInfo->PtrFrameBuffer,       // Destination buffer.
				abs(m_Stride),                    // Destination stride. We use the absolute value to flip bitmaps with negative stride. 
				m_Stride > 0 ? data : data + (m_FrameSize.cy - 1) * abs(m_Stride), // First row in source image with positive stride, or the last row with negative stride.
				m_Stride,						  // Source stride.
				bytesPerPixel * m_FrameSize.cx,	      // Image width in bytes.
				m_FrameSize.cy						  // Image height in pixels.
			);

			pFrameInfo->Stride = abs(m_Stride);
			pFrameInfo->LastTimeStamp = m_LastGrabTimeStamp;
			pFrameInfo->Width = m_FrameSize.cx;
			pFrameInfo->Height = m_FrameSize.cy;
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

HRESULT SourceReaderBase::ResizeFrameBuffer(FRAME_INFO *FrameInfo, int bufferSize) {
	// Old buffer too small
	if (bufferSize > (int)FrameInfo->BufferSize)
	{
		if (FrameInfo->PtrFrameBuffer)
		{
			delete[] FrameInfo->PtrFrameBuffer;
			FrameInfo->PtrFrameBuffer = nullptr;
		}
		FrameInfo->PtrFrameBuffer = new (std::nothrow) BYTE[bufferSize];
		if (!FrameInfo->PtrFrameBuffer)
		{
			FrameInfo->BufferSize = 0;
			LOG_ERROR(L"Failed to allocate memory for frame");
			return E_OUTOFMEMORY;
		}

		// Update buffer size
		FrameInfo->BufferSize = bufferSize;
	}
	return S_OK;
}

HRESULT SourceReaderBase::Initialize(_In_ DX_RESOURCES *Data)
{
	m_Device = Data->Device;
	m_DeviceContext = Data->Context;

	m_Device->AddRef();
	m_DeviceContext->AddRef();
	return S_OK;
}

HRESULT SourceReaderBase::GetFrameSize(_In_ IMFMediaType *pMediaType, _Out_ SIZE *pFrameSize)
{
	UINT32 width;
	UINT32 height;
	//Get width and height
	RETURN_ON_BAD_HR(MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height));
	*pFrameSize = SIZE{ (LONG)width,(LONG)height };
	return S_OK;
}

HRESULT SourceReaderBase::GetFrameRate(_In_ IMFMediaType *pMediaType, _Out_ double *pFramerate)
{
	UINT32 numerator;
	UINT32 denominator;
	RETURN_ON_BAD_HR(MFGetAttributeRatio(
		pMediaType,
		MF_MT_FRAME_RATE,
		&numerator,
		&denominator
	));
	double framerate = (double)numerator / denominator;
	*pFramerate = framerate;
	return S_OK;
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

//Calculates the default stride based on the format and size of the frames
HRESULT SourceReaderBase::GetDefaultStride(_In_ IMFMediaType *type, _Out_ LONG *stride)
{
	LONG tempStride = 0;

	// Try to get the default stride from the media type.
	HRESULT hr = type->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32 *)&tempStride);
	if (FAILED(hr))
	{
		//Setting this atribute to NULL we can obtain the default stride
		GUID subtype = GUID_NULL;

		UINT32 width = 0;
		UINT32 height = 0;

		// Obtain the subtype
		hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
		//obtain the width and height
		if (SUCCEEDED(hr))
			hr = MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
		//Calculate the stride based on the subtype and width
		if (SUCCEEDED(hr))
			hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &tempStride);
		// set the attribute so it can be read
		if (SUCCEEDED(hr))
			(void)type->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(tempStride));
	}

	if (SUCCEEDED(hr))
		*stride = tempStride;
	return hr;
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

	//IMFMediaType *pOutputMediaType;
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
	if (FAILED(hr))
	{
		return hr;
	}
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
				hr = m_MediaTransform->GetOutputStreamInfo(streamIndex, &info);
				if (FAILED(hr)) {
					LOG_ERROR(L"GetOutputStreamInfo failed: hr = 0x%08x", hr);
				}
				IMFMediaBuffer *transformBuffer;
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
				MFT_OUTPUT_DATA_BUFFER outputDataBuffer{};
				outputDataBuffer.dwStreamID = streamIndex;
				outputDataBuffer.pSample = transformSample;

				hr = m_MediaTransform->ProcessInput(streamIndex, sample, 0);
				if (FAILED(hr)) {
					LOG_ERROR(L"ProcessInput failed: hr = 0x%08x", hr);
				}
				DWORD dwDSPStatus = 0;
				hr = m_MediaTransform->ProcessOutput(0, 1, &outputDataBuffer, &dwDSPStatus);
				if (FAILED(hr)) {
					LOG_ERROR(L"ProcessOutput failed: hr = 0x%08x", hr);
				}
				SafeRelease(&m_Sample);
				SafeRelease(&transformBuffer);
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
				m_FramerateTimer->StartRecurringTimer((INT64)round(m_FrameRate));
			}
			if (m_FrameRate > 0) {
				auto t1 = std::chrono::high_resolution_clock::now();
				auto sleepTime = m_FramerateTimer->GetMillisUntilNextTick();
				//LOG_TRACE("OnReadSample waiting for %.2f ms", sleepTime);
				MeasureExecutionTime measureNextTick(L"OnReadSample scheduled delay");
				hr = m_FramerateTimer->WaitForNextTick();
				if (FAILED(hr)) {
					return hr;
				}

				auto t2 = std::chrono::high_resolution_clock::now();
				std::chrono::duration<double, std::milli> ms_double = t2 - t1;
				double diff = ms_double.count() - sleepTime;
				measureNextTick.SetName(string_format(L"OnReadSample scheduled delay for %.2f ms for next frame. Actual delay differed by: %.2f ms, with a total delay of", sleepTime, diff));
			}
		}
	}
	// Request the next frame.
	if (m_SourceReader) {
		hr = m_SourceReader->ReadSample(streamIndex, 0, NULL, NULL, NULL, NULL);
	}
	return hr;
}
//Method from IMFSourceReaderCallback 
STDMETHODIMP SourceReaderBase::OnEvent(DWORD, IMFMediaEvent *) { return S_OK; }
//Method from IMFSourceReaderCallback 
STDMETHODIMP SourceReaderBase::OnFlush(DWORD) { return S_OK; }
#include "video_capture.h"
#include "cleanup.h"
#include <Mferror.h>

video_capture::video_capture() :
	m_Sample{ nullptr },
	m_InputMediaType(nullptr),
	m_OutputMediaType(nullptr),
	m_ColorConverter(nullptr),
	m_BytesPerPixel(0),
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_InputVideoFormat{ 0 },
	m_LastSampleReceivedTimeStamp{ 0 },
	m_LastGrabTimeStamp{ 0 },
	m_Stride{ 0 }
{
	InitializeCriticalSection(&criticalSection);
	referenceCount = 1;
	m_Width = 0;
	m_Height = 0;
	m_SourceReader = NULL;
}
video_capture::~video_capture()
{
	Close();
	EnterCriticalSection(&criticalSection);
	SafeRelease(&m_Device);
	SafeRelease(&m_DeviceContext);
	LeaveCriticalSection(&criticalSection);
	DeleteCriticalSection(&criticalSection);
}

HRESULT video_capture::StartCapture(_In_ std::wstring deviceName)
{
	HRESULT hr;

	UINT32 count = 0;
	CComPtr<IMFAttributes> pAttributes = nullptr;
	IMFActivate **ppDevices = NULL;
	ReleaseCOMArrayOnExit releaseDevicesOnExit((IUnknown**)ppDevices, count);
	// Create an attribute store to specify enumeration parameters.
	RETURN_ON_BAD_HR(hr = MFCreateAttributes(&pAttributes, 1));

	//The attribute to be requested is devices that can capture video
	RETURN_ON_BAD_HR(hr = pAttributes->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
	));
	//Enummerate the video capture devices
	RETURN_ON_BAD_HR(hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count));
	releaseDevicesOnExit.SetCount(count);
	//if there are any available devices
	for (UINT32 i = 0; i < count; i++)
	{
		WCHAR *symbolicLink;
		UINT32 cchSymbolicLink;
		IMFActivate *pDevice = ppDevices[i];
		hr = pDevice->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symbolicLink, &cchSymbolicLink);
		CoTaskMemFreeOnExit freeSymbolicLink(symbolicLink);
		if (!deviceName.empty() && deviceName != symbolicLink) {
			continue;
		}
		m_DeviceSymbolicLink = std::wstring(symbolicLink);
		hr = InitializeSourceReader(pDevice);
		if (FAILED(hr)) {
			continue;
		}
		hr = CreateOutputMediaType(&m_OutputMediaType);
		if (FAILED(hr)) {
			break;
		}
		GetDefaultStride(m_OutputMediaType, &m_Stride);
		hr = CreateColorConverter(&m_ColorConverter);
		if (FAILED(hr)) {
			break;
		}

		WCHAR *nameString = NULL;
		// Get the human-friendly name of the device
		UINT32 cchName;
		hr = pDevice->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
			&nameString, &cchName);

		if (SUCCEEDED(hr))
		{
			//allocate a byte buffer for the raw pixel data
			m_BytesPerPixel = abs(m_Stride) / m_Width;
			m_DeviceName = std::wstring(nameString);
		}
		CoTaskMemFree(nameString);
		break;
	}

	if (SUCCEEDED(hr))
	{
		// Ask for the first sample.
		hr = m_SourceReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, NULL, NULL, NULL);
	}

	return hr;
}

HRESULT video_capture::GetFrame(_In_ SIZE size, _Outptr_ ID3D11Texture2D **pFrame)
{
	HRESULT hr;
	EnterCriticalSection(&criticalSection);
	if (m_LastSampleReceivedTimeStamp.QuadPart > m_LastGrabTimeStamp.QuadPart)
	{
		BYTE* data;
		m_Sample->Lock(&data, NULL, NULL);
		D3D11_TEXTURE2D_DESC desc{};
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.Width = m_Width;
		desc.Height = m_Height;

		D3D11_SUBRESOURCE_DATA initData = { 0 };
		initData.pSysMem = data;
		initData.SysMemPitch = abs(m_Stride);
		initData.SysMemSlicePitch = 0;

		ID3D11Texture2D* tex = nullptr;
		hr = m_Device->CreateTexture2D(&desc, &initData, &tex);
		if (SUCCEEDED(hr)) {
			if (pFrame) {
				*pFrame = tex;
				(*pFrame)->AddRef();
			}
			QueryPerformanceCounter(&m_LastGrabTimeStamp);
		}
		else {
			SafeRelease(&tex);
		}
		m_Sample->Unlock();
	}
	else {
		hr = DXGI_ERROR_WAIT_TIMEOUT;
	}
	LeaveCriticalSection(&criticalSection);

	return hr;
}

HRESULT video_capture::GetFrameBuffer(_Inout_ FRAME_INFO *pFrameInfo)
{
	HRESULT hr;
	EnterCriticalSection(&criticalSection);
	if (m_LastSampleReceivedTimeStamp.QuadPart > m_LastGrabTimeStamp.QuadPart)
	{
		DWORD len;
		BYTE* data;
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
			memcpy(pFrameInfo->PtrFrameBuffer, data, len);
			pFrameInfo->Stride = m_Stride;
			pFrameInfo->LastTimeStamp = m_LastGrabTimeStamp;
			pFrameInfo->Width = m_Width;
			pFrameInfo->Height = m_Height;
		}
	}
	else {
		hr = DXGI_ERROR_WAIT_TIMEOUT;
	}
	LeaveCriticalSection(&criticalSection);

	return hr;
}

HRESULT video_capture::ResizeFrameBuffer(FRAME_INFO* FrameInfo, int bufferSize) {
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
			LOG_ERROR(L"Failed to allocate memory for frame in video_capture");
			return E_OUTOFMEMORY;
		}

		// Update buffer size
		FrameInfo->BufferSize = bufferSize;
	}
	return S_OK;
}

HRESULT video_capture::Initialize(_In_ DX_RESOURCES *Data)
{
	m_Device = Data->Device;
	m_DeviceContext = Data->Context;

	m_Device->AddRef();
	m_DeviceContext->AddRef();
	return S_OK;
}


HRESULT video_capture::InitializeSourceReader(_In_ IMFActivate *pDevice)
{
	HRESULT hr = S_OK;
	CComPtr<IMFMediaSource> source = nullptr;
	CComPtr<IMFAttributes> attributes = nullptr;
	EnterCriticalSection(&criticalSection);

	hr = pDevice->ActivateObject(__uuidof(IMFMediaSource), (void**)&source);

	//Allocate attributes
	if (SUCCEEDED(hr))
		hr = MFCreateAttributes(&attributes, 2);
	//get attributes
	if (SUCCEEDED(hr))
		hr = attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
	// Set the callback pointer.
	if (SUCCEEDED(hr))
		hr = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, this);
	//Create the source reader
	if (SUCCEEDED(hr))
		hr = MFCreateSourceReaderFromMediaSource(source, attributes, &m_SourceReader);
	// Try to find a suitable output type.
	if (SUCCEEDED(hr))
	{
		for (DWORD i = 0; ; i++)
		{
			SafeRelease(&m_OutputMediaType);
			SafeRelease(&m_InputMediaType);
			hr = m_SourceReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &m_InputMediaType);
			if (FAILED(hr))
			{
				break;
			}
			//Check if the input video format is supported
			hr = IsMediaTypeSupported(m_InputMediaType);
			if (FAILED(hr))
			{
				break;
			}
			//Get the stride for this format so we can calculate the number of bytes per pixel
			GetDefaultStride(m_InputMediaType, &m_Stride);

			m_InputMediaType->GetGUID(MF_MT_SUBTYPE, &m_InputVideoFormat);

			//Get width and height
			MFGetAttributeSize(m_InputMediaType, MF_MT_FRAME_SIZE, &m_Width, &m_Height);

			if (SUCCEEDED(hr))// Found an output type.
				break;
		}
	}


	if (FAILED(hr))
	{
		if (source)
		{
			source->Shutdown();
		}
		Close();
	}
	LeaveCriticalSection(&criticalSection);
	return hr;
}

HRESULT video_capture::IsMediaTypeSupported(_In_ IMFMediaType *pType)
{
	GUID subtype{};

	RETURN_ON_BAD_HR(pType->GetGUID(MF_MT_SUBTYPE, &subtype));

	if (subtype == MFVideoFormat_RGB32 || subtype == MFVideoFormat_RGB24 || subtype == MFVideoFormat_YUY2 || subtype == MFVideoFormat_NV12)
		return S_OK;
	else
		return S_FALSE;

}

HRESULT video_capture::Close()
{
	if (m_SourceReader)
	{
		m_SourceReader->Release();
		m_SourceReader = NULL;
	}
	SafeRelease(&m_InputMediaType);
	SafeRelease(&m_OutputMediaType);
	SafeRelease(&m_ColorConverter);
	SafeRelease(&m_Sample);
	return S_OK;
}

//From IUnknown 
STDMETHODIMP video_capture::QueryInterface(REFIID riid, void** ppvObject)
{
	static const QITAB qit[] = { QITABENT(video_capture, IMFSourceReaderCallback),{ 0 }, };
	return QISearch(this, qit, riid, ppvObject);
}
//From IUnknown
ULONG video_capture::Release()
{
	ULONG count = InterlockedDecrement(&referenceCount);
	if (count == 0)
		delete this;
	// For thread safety
	return count;
}
//From IUnknown
ULONG video_capture::AddRef()
{
	return InterlockedIncrement(&referenceCount);
}

//Calculates the default stride based on the format and size of the frames
HRESULT video_capture::GetDefaultStride(_In_ IMFMediaType *type, _Out_ LONG *stride)
{
	LONG tempStride = 0;

	// Try to get the default stride from the media type.
	HRESULT hr = type->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&tempStride);
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

HRESULT video_capture::CreateOutputMediaType(_Outptr_ IMFMediaType **pType)
{
	*pType = nullptr;
	HRESULT hr;
	CComPtr<IMFMediaType> pIntermediateMediaType = nullptr;
	//DSP output MediaType
	RETURN_ON_BAD_HR(hr = MFCreateMediaType(&pIntermediateMediaType));
	RETURN_ON_BAD_HR(hr = pIntermediateMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	RETURN_ON_BAD_HR(hr = pIntermediateMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32));
	RETURN_ON_BAD_HR(hr = pIntermediateMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	RETURN_ON_BAD_HR(hr = MFSetAttributeSize(pIntermediateMediaType, MF_MT_FRAME_SIZE, m_Width, m_Height));
	RETURN_ON_BAD_HR(hr = MFSetAttributeRatio(pIntermediateMediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));

	if (pType) {
		*pType = pIntermediateMediaType;
		(*pType)->AddRef();
	}
	return hr;
}

HRESULT video_capture::CreateColorConverter(_Outptr_ IMFTransform **pColorConverter)
{
	*pColorConverter = nullptr;
	IMFTransform *pConverter = NULL;
	HRESULT hr;
	RETURN_ON_BAD_HR(hr = CoCreateInstance(CLSID_VideoProcessorMFT, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pConverter)));
	RETURN_ON_BAD_HR(hr = pConverter->SetInputType(0, m_InputMediaType, 0));
	GUID guidMinor;
	GUID guidMajor;
	for (int i = 0; i < 8; i++)
	{
		IMFMediaType *mediaType;
		hr = pConverter->GetOutputAvailableType(0, i, &mediaType);
		if (hr == MF_E_NO_MORE_TYPES)
		{
			break;
		}
		hr = mediaType->GetGUID(MF_MT_MAJOR_TYPE, &guidMajor);
		hr = mediaType->GetGUID(MF_MT_SUBTYPE, &guidMinor);
		if (guidMinor == MFVideoFormat_ARGB32) {
			break;
		}
		mediaType->Release();
	}
	RETURN_ON_BAD_HR(hr = pConverter->SetOutputType(0, m_OutputMediaType, 0));
	if (pColorConverter) {
		*pColorConverter = pConverter;
		(*pColorConverter)->AddRef();
	}
	return hr;
}

//Method from IMFSourceReaderCallback
HRESULT video_capture::OnReadSample(HRESULT status, DWORD streamIndex, DWORD streamFlags, LONGLONG timeStamp, IMFSample *sample)
{
	HRESULT hr = S_OK;

	EnterCriticalSection(&criticalSection);

	if (FAILED(status))
		hr = status;

	if (SUCCEEDED(hr))
	{
		if (sample)
		{
			if (SUCCEEDED(hr))
			{
				IMFSample *transformSample;
				IMFMediaBuffer *transformBuffer;
				hr = MFCreateSample(&transformSample);
				hr = MFCreateMemoryBuffer(m_Width * m_Height * m_BytesPerPixel, &transformBuffer);
				hr = transformSample->AddBuffer(transformBuffer);

				DWORD dwDSPStatus = 0;
				MFT_OUTPUT_DATA_BUFFER outputDataBuffer;
				outputDataBuffer.dwStreamID = 0;
				outputDataBuffer.dwStatus = 0;
				outputDataBuffer.pEvents = NULL;
				outputDataBuffer.pSample = transformSample;
				MFT_INPUT_STREAM_INFO info;
				hr = m_ColorConverter->GetInputStreamInfo(0, &info);
				hr = m_ColorConverter->ProcessInput(0, sample, 0);
				hr = m_ColorConverter->ProcessOutput(0, 1, &outputDataBuffer, &dwDSPStatus);

				SafeRelease(&m_Sample);
				SafeRelease(&transformBuffer);
				IMFMediaBuffer* mediaBuffer = NULL;
				outputDataBuffer.pSample->GetBufferByIndex(0, &mediaBuffer);
				m_Sample = mediaBuffer;
				QueryPerformanceCounter(&m_LastSampleReceivedTimeStamp);
				outputDataBuffer.pSample->Release();
			}
		}
	}
	// Request the next frame.
	if (SUCCEEDED(hr))
		hr = m_SourceReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, NULL, NULL, NULL);

	if (FAILED(hr))
	{
		//Notify there was an error
		printf("Error HRESULT = 0x%d", hr);
		PostMessage(NULL, 1, (WPARAM)hr, 0L);
	}
	LeaveCriticalSection(&criticalSection);
	return hr;
}
//Method from IMFSourceReaderCallback 
STDMETHODIMP video_capture::OnEvent(DWORD, IMFMediaEvent *) { return S_OK; }
//Method from IMFSourceReaderCallback 
STDMETHODIMP video_capture::OnFlush(DWORD) { return S_OK; }
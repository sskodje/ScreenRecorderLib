#include "capture_base.h"
#include "cleanup.h"

DWORD WINAPI OverlayProc(_In_ void* Param);

capture_base::capture_base(_In_ ID3D11Device * pDevice, _In_  ID3D11DeviceContext * pDeviceContext) :
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_TerminateThreadsEvent(nullptr),
	m_LastAcquiredFrameTimeStamp{},
	m_OutputRect{},
	m_SharedSurf(nullptr),
	m_KeyMutex(nullptr),
	m_OverlayThreadCount(0),
	m_OverlayThreadHandles(nullptr),
	m_OverlayThreadData(nullptr),
	m_CaptureThreadCount(0),
	m_CaptureThreadHandles(nullptr),
	m_CaptureThreadData(nullptr)
{
	RtlZeroMemory(&m_PtrInfo, sizeof(m_PtrInfo));
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;
	// Event to tell spawned threads to quit
	m_TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

capture_base::~capture_base()
{
	Clean();
}

HRESULT capture_base::StartOverlayCapture(_In_ std::vector<RECORDING_OVERLAY> overlays, _In_ HANDLE hUnexpectedErrorEvent, _In_ HANDLE hExpectedErrorEvent)
{
	ResetEvent(m_TerminateThreadsEvent);

	m_OverlayThreadCount = (UINT)(overlays.size());
	m_OverlayThreadHandles = new (std::nothrow) HANDLE[m_OverlayThreadCount]{};
	m_OverlayThreadData = new (std::nothrow) OVERLAY_THREAD_DATA[m_OverlayThreadCount]{};
	if (!m_OverlayThreadHandles || !m_OverlayThreadData)
	{
		return E_OUTOFMEMORY;
	}
	for (int i = 0; i < m_OverlayThreadCount; i++)
	{
		auto overlay = overlays.at(i);
		m_OverlayThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_OverlayThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_OverlayThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;

		m_OverlayThreadData[i].RecordingOverlay = new RECORDING_OVERLAY_DATA();
		m_OverlayThreadData[i].RecordingOverlay->CaptureDevice = overlay.CaptureDevice;
		m_OverlayThreadData[i].RecordingOverlay->Position = overlay.Position;
		m_OverlayThreadData[i].RecordingOverlay->Size = overlay.Size;
		m_OverlayThreadData[i].RecordingOverlay->Type = overlay.Type;
		m_OverlayThreadData[i].RecordingOverlay->FrameInfo = new FRAME_INFO();
		RtlZeroMemory(&m_OverlayThreadData[i].RecordingOverlay->DxRes, sizeof(DX_RESOURCES));
		HRESULT hr = InitializeDx(&m_OverlayThreadData[i].RecordingOverlay->DxRes);
		if (FAILED(hr))
		{
			return hr;
		}

		DWORD ThreadId;
		m_OverlayThreadHandles[i] = CreateThread(nullptr, 0, OverlayProc, &m_OverlayThreadData[i], 0, &ThreadId);
		if (m_OverlayThreadHandles[i] == nullptr)
		{
			return E_FAIL;
		}
		i++;
	}
	return S_OK;
}

//
// Start up threads for DDA
//
HRESULT capture_base::StartCapture(_In_ LPTHREAD_START_ROUTINE captureThreadProc, _In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent)
{
	HRESULT hr = StartOverlayCapture(overlays, hUnexpectedErrorEvent, hExpectedErrorEvent);
	RETURN_ON_BAD_HR(hr);
	std::vector<RECORDING_SOURCE_DATA*> CreatedOutputs{};
	RETURN_ON_BAD_HR(hr = CreateSharedSurf(sources, &CreatedOutputs, &m_OutputRect));
	m_CaptureThreadCount = (UINT)(CreatedOutputs.size());
	m_CaptureThreadHandles = new (std::nothrow) HANDLE[m_CaptureThreadCount]{};
	m_CaptureThreadData = new (std::nothrow) CAPTURE_THREAD_DATA[m_CaptureThreadCount]{};
	if (!m_CaptureThreadHandles || !m_CaptureThreadData)
	{
		return E_OUTOFMEMORY;
	}
	HANDLE sharedHandle = GetSharedHandle();
	// Create appropriate # of threads for duplication

	for (int i = 0; i< m_CaptureThreadCount; i++)
	{
		RECORDING_SOURCE_DATA *data = CreatedOutputs.at(i);
		m_CaptureThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_CaptureThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_CaptureThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_CaptureThreadData[i].TexSharedHandle = sharedHandle;
		m_CaptureThreadData[i].PtrInfo = &m_PtrInfo;

		m_CaptureThreadData[i].RecordingSource = data;

		RtlZeroMemory(&m_CaptureThreadData[i].RecordingSource->DxRes, sizeof(DX_RESOURCES));
		RETURN_ON_BAD_HR(hr = InitializeDx(&m_CaptureThreadData[i].RecordingSource->DxRes));

		DWORD ThreadId;
		m_CaptureThreadHandles[i] = CreateThread(nullptr, 0, captureThreadProc, &m_CaptureThreadData[i], 0, &ThreadId);
		if (m_CaptureThreadHandles[i] == nullptr)
		{
			return E_FAIL;
		}
	}

	return hr;
}

HRESULT capture_base::StopCapture()
{
	if (!SetEvent(m_TerminateThreadsEvent)) {
		LOG_ERROR("Could not terminate capture thread");
		return E_FAIL;
	}
	WaitForThreadTermination();
	return S_OK;
}

HRESULT capture_base::AcquireNextFrame(_In_  DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame)
{
	// Try to acquire keyed mutex in order to access shared surface
	HRESULT hr = m_KeyMutex->AcquireSync(1, timeoutMillis);
	if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
	{
		return DXGI_ERROR_WAIT_TIMEOUT;
	}
	else if (FAILED(hr))
	{
		return hr;
	}
	ReleaseKeyedMutexOnExit releaseMutex(m_KeyMutex, 0);

	bool haveNewFrameData = IsUpdatedFramesAvailable() && IsInitialFrameWriteComplete();
	if (!haveNewFrameData) {
		return DXGI_ERROR_WAIT_TIMEOUT;
	}

	UINT updatedFrameCount = GetUpdatedFrameCount(true);
	RETURN_ON_BAD_HR(hr = ProcessOverlays());
	ID3D11Texture2D *pDesktopFrame = nullptr;
	D3D11_TEXTURE2D_DESC desc;
	m_SharedSurf->GetDesc(&desc);
	desc.MiscFlags = 0;
	RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pDesktopFrame));
	m_DeviceContext->CopyResource(pDesktopFrame, m_SharedSurf);


	QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);

	pFrame->Frame = pDesktopFrame;
	pFrame->PtrInfo = &m_PtrInfo;
	pFrame->UpdateCount = updatedFrameCount;
	pFrame->Timestamp = m_LastAcquiredFrameTimeStamp;
	pFrame->ContentSize = GetContentSize();
	return hr;
}

//
// Clean up resources
//
void capture_base::Clean()
{
	if (m_SharedSurf) {
		m_SharedSurf->Release();
		m_SharedSurf = nullptr;
	}
	if (m_KeyMutex)
	{
		m_KeyMutex->Release();
		m_KeyMutex = nullptr;
	}
	if (m_PtrInfo.PtrShapeBuffer)
	{
		delete[] m_PtrInfo.PtrShapeBuffer;
		m_PtrInfo.PtrShapeBuffer = nullptr;
	}
	RtlZeroMemory(&m_PtrInfo, sizeof(m_PtrInfo));

	if (m_OverlayThreadHandles) {
		for (UINT i = 0; i < m_OverlayThreadCount; ++i)
		{
			if (m_OverlayThreadHandles[i])
			{
				CloseHandle(m_OverlayThreadHandles[i]);
			}
		}
		delete[] m_OverlayThreadHandles;
		m_OverlayThreadHandles = nullptr;
	}

	if (m_OverlayThreadData)
	{
		for (UINT i = 0; i < m_OverlayThreadCount; ++i)
		{
			if (m_OverlayThreadData[i].RecordingOverlay) {
				CleanDx(&m_OverlayThreadData[i].RecordingOverlay->DxRes);
				if (m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer) {
					delete[] m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer;
					m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer = nullptr;
				}
				delete m_OverlayThreadData[i].RecordingOverlay->FrameInfo;
				delete m_OverlayThreadData[i].RecordingOverlay;
				m_OverlayThreadData[i].RecordingOverlay = nullptr;
			}
		}
		delete[] m_OverlayThreadData;
		m_OverlayThreadData = nullptr;
	}

	m_OverlayThreadCount = 0;

	if (m_CaptureThreadHandles) {
		for (UINT i = 0; i < m_CaptureThreadCount; ++i)
		{
			if (m_CaptureThreadHandles[i])
			{
				CloseHandle(m_CaptureThreadHandles[i]);
			}
		}
		delete[] m_CaptureThreadHandles;
		m_CaptureThreadHandles = nullptr;
	}

	if (m_CaptureThreadData)
	{
		for (UINT i = 0; i < m_CaptureThreadCount; ++i)
		{
			if (m_CaptureThreadData[i].RecordingSource) {
				CleanDx(&m_CaptureThreadData[i].RecordingSource->DxRes);
				delete m_CaptureThreadData[i].RecordingSource;
				m_CaptureThreadData[i].RecordingSource = nullptr;
			}
		}
		delete[] m_CaptureThreadData;
		m_CaptureThreadData = nullptr;
	}

	m_CaptureThreadCount = 0;

	CloseHandle(m_TerminateThreadsEvent);
}

//
// Waits infinitely for all spawned threads to terminate
//
void capture_base::WaitForThreadTermination()
{
	if (m_OverlayThreadCount != 0)
	{
		WaitForMultipleObjectsEx(m_OverlayThreadCount, m_OverlayThreadHandles, TRUE, INFINITE, FALSE);
	}
	if (m_CaptureThreadCount != 0) {
		WaitForMultipleObjectsEx(m_CaptureThreadCount, m_CaptureThreadHandles, TRUE, INFINITE, FALSE);
	}
}



//
// Returns shared handle
//
_Ret_maybenull_ HANDLE capture_base::GetSharedHandle()
{
	if (!m_SharedSurf) {
		return nullptr;
	}
	HANDLE Hnd = nullptr;

	// QI IDXGIResource interface to synchronized shared surface.
	IDXGIResource* DXGIResource = nullptr;
	HRESULT hr = m_SharedSurf->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&DXGIResource));
	if (SUCCEEDED(hr))
	{
		// Obtain handle to IDXGIResource object.
		DXGIResource->GetSharedHandle(&Hnd);
		DXGIResource->Release();
		DXGIResource = nullptr;
	}

	return Hnd;
}


bool capture_base::IsUpdatedFramesAvailable()
{
	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			return true;
		}
	}
	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		if (m_CaptureThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			return true;
		}
	}
	return false;
}

bool capture_base::IsInitialFrameWriteComplete()
{
	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		if (m_CaptureThreadData[i].RecordingSource) {
			if (m_CaptureThreadData[i].TotalUpdatedFrameCount == 0) {
				//If any of the recordings have not yet written a frame, we return and wait for them.
				return false;
			}
		}
	}
	return true;
}

UINT capture_base::GetUpdatedFrameCount(_In_ bool resetUpdatedFrameCounts)
{
	int updatedFrameCount = 0;

	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		if (m_CaptureThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			updatedFrameCount += m_CaptureThreadData[i].UpdatedFrameCountSinceLastWrite;
			if (resetUpdatedFrameCounts) {
				m_CaptureThreadData[i].UpdatedFrameCountSinceLastWrite = 0;
			}
		}
	}
	return updatedFrameCount;
}


HRESULT capture_base::CreateSharedSurf(_In_ std::vector<RECORDING_SOURCE> sources, _Out_ std::vector<RECORDING_SOURCE_DATA*> *pCreatedOutputs, _Out_ RECT* pDeskBounds)
{
	*pCreatedOutputs = std::vector<RECORDING_SOURCE_DATA*>();
	std::vector<std::pair<RECORDING_SOURCE, RECT>> validOutputs{};
	int xOffset;
	for each (RECORDING_SOURCE source in sources)
	{
		switch (source.Type)
		{
		case SourceType::Monitor: {
			// Figure out right dimensions for full size desktop texture and # of outputs to duplicate
			DXGI_OUTPUT_DESC outputDesc{};
			CComPtr<IDXGIOutput> output;
			HRESULT hr = GetOutputForDeviceName(source.CaptureDevice, &output);
			if (FAILED(hr))
			{
				LOG_ERROR(L"Failed to get output descs for selected devices");
				return hr;
			}
			output->GetDesc(&outputDesc);
			std::pair<RECORDING_SOURCE, RECT> tuple(source, outputDesc.DesktopCoordinates);
			validOutputs.push_back(tuple);
			break;
		}
		case SourceType::Window: {
			RECT rect;
			if (GetWindowRect(source.WindowHandle, &rect))
			{
				if (validOutputs.size() > 0) {
					rect.right = validOutputs.back().second.right + (rect.right - rect.left);
				}
				std::pair<RECORDING_SOURCE, RECT> tuple(source, rect);
				validOutputs.push_back(tuple);
				break;
			}
			break;
		}
		default:
			break;
		}
	}
	std::sort(validOutputs.begin(), validOutputs.end(), compareOutputs);

	std::vector<RECT> outputRects{};
	for each (auto pair in validOutputs)
	{
		outputRects.push_back(pair.second);
	}
	std::vector<SIZE> outputOffsets{};
	GetCombinedRects(outputRects, pDeskBounds, &outputOffsets);

	pDeskBounds = &MakeRectEven(*pDeskBounds);
	for (int i = 0; i < validOutputs.size(); i++)
	{
		RECORDING_SOURCE source = validOutputs.at(i).first;
		RECORDING_SOURCE_DATA *data = new RECORDING_SOURCE_DATA();
		data->OutputMonitor = source.CaptureDevice;
		data->OutputWindow = source.WindowHandle;
		data->IsCursorCaptureEnabled = source.IsCursorCaptureEnabled;
		data->OffsetX = pDeskBounds->left + outputOffsets.at(i).cx;
		data->OffsetY = pDeskBounds->top + outputOffsets.at(i).cy;
		pCreatedOutputs->push_back(data);
	}

	// Set created outputs
	return capture_base::CreateSharedSurf(*pDeskBounds);
}

HRESULT capture_base::CreateSharedSurf(_In_ RECT desktopRect)
{
	// Create shared texture for all capture threads to draw into
	D3D11_TEXTURE2D_DESC DeskTexD;
	RtlZeroMemory(&DeskTexD, sizeof(D3D11_TEXTURE2D_DESC));
	DeskTexD.Width = desktopRect.right - desktopRect.left;
	DeskTexD.Height = desktopRect.bottom - desktopRect.top;
	DeskTexD.MipLevels = 1;
	DeskTexD.ArraySize = 1;
	DeskTexD.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	DeskTexD.SampleDesc.Count = 1;
	DeskTexD.Usage = D3D11_USAGE_DEFAULT;
	DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	DeskTexD.CPUAccessFlags = 0;
	DeskTexD.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

	HRESULT hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_SharedSurf);
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to create shared texture");
		return hr;
	}
	// Get keyed mutex
	hr = m_SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&m_KeyMutex));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to query for keyed mutex in OUTPUTMANAGER");
		return hr;
	}

	return hr;
}

SIZE capture_base::GetContentSize()
{
	RECT combinedRect{};
	std::vector<RECT> contentRects{};
	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		contentRects.push_back(m_CaptureThreadData[i].ContentFrameRect);
	}
	GetCombinedRects(contentRects, &combinedRect, nullptr);
	return SIZE{ combinedRect.right - combinedRect.left,combinedRect.bottom - combinedRect.top };
}

HRESULT capture_base::ProcessOverlays()
{
	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].RecordingOverlay) {
			RECORDING_OVERLAY_DATA *pOverlay = m_OverlayThreadData[i].RecordingOverlay;
			if (pOverlay->FrameInfo && pOverlay->FrameInfo->PtrFrameBuffer) {
				FRAME_INFO *frameInfo = pOverlay->FrameInfo;
				//Create new frame
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
				desc.Width = frameInfo->Width;
				desc.Height = frameInfo->Height;

				D3D11_SUBRESOURCE_DATA initData = { 0 };
				initData.pSysMem = frameInfo->PtrFrameBuffer;
				initData.SysMemPitch = abs(frameInfo->Stride);
				initData.SysMemSlicePitch = 0;

				CComPtr<ID3D11Texture2D> pOverlayTexture;
				HRESULT hr = m_Device->CreateTexture2D(&desc, &initData, &pOverlayTexture);
				if (SUCCEEDED(hr)) {
					D3D11_BOX Box{};
					// Copy back to shared surface
					Box.right = desc.Width;
					Box.bottom = desc.Height;
					Box.back = 1;
					m_DeviceContext->CopySubresourceRegion(m_SharedSurf, 0, 0, 0, 0, pOverlayTexture, 0, &Box);
				}
			}
		}
	}
	return S_OK;
}


DWORD WINAPI OverlayProc(_In_ void* Param) {
	HRESULT hr = S_OK;
	// Data passed in from thread creation
	OVERLAY_THREAD_DATA* pData = reinterpret_cast<OVERLAY_THREAD_DATA*>(Param);

	bool isExpectedError = false;
	bool isUnexpectedError = false;

	RECORDING_OVERLAY_DATA *pOverlay = pData->RecordingOverlay;
	switch (pOverlay->Type)
	{
	case OverlayType::Picture: {

		break;
	}
	case OverlayType::Video: {

		break;
	}
	case OverlayType::VideoCapture: {
		// Classes
		video_capture pVideoCaptureManager{};

		hr = pVideoCaptureManager.Initialize(&pOverlay->DxRes);
		//This scope must be here for ReleaseOnExit to work.
		{
			CloseVideoCaptureOnExit closeCapture(&pVideoCaptureManager);
			hr = pVideoCaptureManager.StartCapture(pOverlay->CaptureDevice);
			if (FAILED(hr))
			{
				goto Exit;
			}
			// Main capture loop
			CComPtr<IMFMediaBuffer> pFrameBuffer = nullptr;
			while (true)
			{
				if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
					hr = S_OK;
					break;
				}

				if (pFrameBuffer)
					pFrameBuffer.Release();
				// Get new frame from video capture
				hr = pVideoCaptureManager.GetFrameBuffer(pOverlay->FrameInfo);
				if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
					continue;
				}
				else if (FAILED(hr)) {
					break;
				}

				QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
			}
		}
		break;
	}
	default:
		break;
	}

Exit:
	pData->ThreadResult = hr;
	if (isExpectedError) {
		SetEvent(pData->ExpectedErrorEvent);
	}
	else if (isUnexpectedError) {
		SetEvent(pData->UnexpectedErrorEvent);
	}

	return 0;
}
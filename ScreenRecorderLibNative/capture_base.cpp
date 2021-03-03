#include "capture_base.h"
#include "cleanup.h"

capture_base::capture_base(ID3D11Device * pDevice, ID3D11DeviceContext * pDeviceContext) :
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_TerminateThreadsEvent(nullptr),
	m_LastAcquiredFrameTimeStamp{},
	m_OutputRect{},
	m_SharedSurf(nullptr),
	m_KeyMutex(nullptr),
	m_ThreadCount(0),
	m_ThreadHandles(nullptr),
	m_ThreadData(nullptr)
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

	if (m_ThreadHandles) {
		for (UINT i = 0; i < m_ThreadCount; ++i)
		{
			if (m_ThreadHandles[i])
			{
				CloseHandle(m_ThreadHandles[i]);
			}
		}
		delete[] m_ThreadHandles;
		m_ThreadHandles = nullptr;
	}

	if (m_ThreadData)
	{
		for (UINT i = 0; i < m_ThreadCount; ++i)
		{
			if (m_ThreadData[i].RecordingSource) {
				CleanDx(&m_ThreadData[i].RecordingSource->DxRes);
				delete m_ThreadData[i].RecordingSource;
				m_ThreadData[i].RecordingSource = nullptr;
			}
			if (m_ThreadData[i].RecordingOverlay) {
				CleanDx(&m_ThreadData[i].RecordingOverlay->DxRes);
				if (m_ThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer) {
					delete[] m_ThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer;
					m_ThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer = nullptr;
				}
				delete m_ThreadData[i].RecordingOverlay->FrameInfo;
				delete m_ThreadData[i].RecordingOverlay;
				m_ThreadData[i].RecordingOverlay = nullptr;
			}
		}
		delete[] m_ThreadData;
		m_ThreadData = nullptr;
	}

	m_ThreadCount = 0;
	CloseHandle(m_TerminateThreadsEvent);
}

//
// Waits infinitely for all spawned threads to terminate
//
void capture_base::WaitForThreadTermination()
{
	if (m_ThreadCount != 0)
	{
		WaitForMultipleObjectsEx(m_ThreadCount, m_ThreadHandles, TRUE, INFINITE, FALSE);
	}
}

HRESULT capture_base::AcquireNextFrame(_In_ DWORD timeoutMillis, CAPTURED_FRAME *pFrame)
{
	return S_FALSE;
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

//
// Returns shared handle
//
HANDLE capture_base::GetSharedHandle()
{
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

DWORD WINAPI OverlayProc(_In_ void* Param) {
	HRESULT hr = S_OK;
	// Data passed in from thread creation
	THREAD_DATA* pData = reinterpret_cast<THREAD_DATA*>(Param);

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

		break;
	}
	default:
		break;
	}
	// Classes
	video_capture pVideoCaptureManager{};

	bool isExpectedError = false;
	bool isUnexpectedError = false;

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

			pData->UpdatedFrameCount++;
			QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
		}
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
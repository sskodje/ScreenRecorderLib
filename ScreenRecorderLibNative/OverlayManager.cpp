#include "OverlayManager.h"
#include "cleanup.h"
#include "CameraCapture.h"
#include "VideoReader.h"
#include "GifReader.h"
#include "screengrab.h"
#include "ImageReader.h"
#include "WindowsGraphicsCapture.h"

using namespace std;
using namespace ScreenRecorderLib::Overlays;

DWORD WINAPI OverlayCaptureThreadProc(_In_ void *Param);

OverlayManager::OverlayManager() :
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_TerminateThreadsEvent(nullptr),
	m_OverlayThreadCount(0),
	m_OverlayThreadHandles(nullptr),
	m_OverlayThreadData(nullptr),
	m_TextureManager(nullptr),
	m_LastAcquiredFrameTimeStamp{},
	m_IsCapturing(false)
{
	// Event to tell spawned threads to quit
	m_TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

OverlayManager::~OverlayManager()
{
	if (m_IsCapturing) {
		StopCapture();
	}
	Clean();
}

//
// Initialize shaders for drawing to screen
//
HRESULT OverlayManager::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_TextureManager = make_unique<TextureManager>();
	HRESULT hr = m_TextureManager->Initialize(m_DeviceContext, m_Device);

	return hr;
}

HRESULT OverlayManager::StartCapture(HANDLE sharedCanvasHandle, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent)
{
	HRESULT hr = S_OK;
	ResetEvent(m_TerminateThreadsEvent);
	m_OverlayThreadCount = (UINT)(overlays.size());
	m_OverlayThreadHandles = new (std::nothrow) HANDLE[m_OverlayThreadCount]{};
	m_OverlayThreadData = new (std::nothrow) OVERLAY_THREAD_DATA[m_OverlayThreadCount]{};
	if (!m_OverlayThreadHandles || !m_OverlayThreadData)
	{
		return E_OUTOFMEMORY;
	}
	for (UINT i = 0; i < m_OverlayThreadCount; i++)
	{
		auto overlay = overlays.at(i);
		m_OverlayThreadData[i].CanvasTexSharedHandle = sharedCanvasHandle;
		m_OverlayThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_OverlayThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_OverlayThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_OverlayThreadData[i].RecordingOverlay = new RECORDING_OVERLAY_DATA(overlay);
		RtlZeroMemory(&m_OverlayThreadData[i].RecordingOverlay->DxRes, sizeof(DX_RESOURCES));
		hr = InitializeDx(nullptr, &m_OverlayThreadData[i].RecordingOverlay->DxRes);
		if (FAILED(hr))
		{
			return hr;
		}

		DWORD ThreadId;
		m_OverlayThreadHandles[i] = CreateThread(nullptr, 0, OverlayCaptureThreadProc, &m_OverlayThreadData[i], 0, &ThreadId);
		if (m_OverlayThreadHandles[i] == nullptr)
		{
			return E_FAIL;
		}
	}
	m_IsCapturing = true;
	return hr;
}

HRESULT OverlayManager::StopCapture()
{
	if (!SetEvent(m_TerminateThreadsEvent)) {
		LOG_ERROR("Could not terminate capture thread");
		return E_FAIL;
	}
	WaitForThreadTermination();
	m_IsCapturing = false;
	return S_OK;
}

bool OverlayManager::IsUpdatedFramesAvailable()
{
	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			return true;
		}
	}
	return false;
}

HRESULT OverlayManager::ProcessOverlays(_Inout_ ID3D11Texture2D *pCanvasTexture, _Out_ int *updateCount)
{
	HRESULT hr = S_FALSE;
	int count = 0;

	D3D11_TEXTURE2D_DESC desc;
	pCanvasTexture->GetDesc(&desc);
	SIZE canvasSize = SIZE{ static_cast<LONG>(desc.Width),static_cast<LONG>(desc.Height) };

	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].RecordingOverlay) {
			RECORDING_OVERLAY_DATA *pOverlayData = m_OverlayThreadData[i].RecordingOverlay;
			HANDLE sharedHandle = m_OverlayThreadData[i].OverlayTexSharedHandle;
			if (pOverlayData && sharedHandle) {
				CComPtr<ID3D11Texture2D> pOverlayTexture;
				CONTINUE_ON_BAD_HR(hr = m_Device->OpenSharedResource(sharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&pOverlayTexture)));
				D3D11_TEXTURE2D_DESC overlayDesc;
				pOverlayTexture->GetDesc(&overlayDesc);
				SIZE textureSize = SIZE{ static_cast<LONG>(overlayDesc.Width),static_cast<LONG>(overlayDesc.Height) };
				CONTINUE_ON_BAD_HR(hr = m_TextureManager->DrawTexture(pCanvasTexture, pOverlayTexture, GetOverlayRect(canvasSize, textureSize, pOverlayData)));
				if (m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
					count++;
				}
			}
		}
	}
	if (count > 0) {
		QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);
	}
	*updateCount = count;
	return hr;
}


RECT OverlayManager::GetOverlayRect(_In_ SIZE canvasSize, _In_ SIZE overlayTextureSize, _In_ RECORDING_OVERLAY_DATA *pOverlay)
{
	LONG backgroundWidth = canvasSize.cx;
	LONG backgroundHeight = canvasSize.cy;
	// Clipping adjusted coordinates / dimensions
	LONG overlayWidth = (pOverlay->OutputSize.value_or(SIZE{ 0 })).cx;
	LONG overlayHeight = (pOverlay->OutputSize.value_or(SIZE{ 0 })).cy;
	if (overlayWidth == 0 && overlayHeight == 0) {
		overlayWidth = overlayTextureSize.cx;
		overlayHeight = overlayTextureSize.cy;
	}
	if (overlayWidth == 0) {
		overlayWidth = (LONG)(overlayTextureSize.cx * ((FLOAT)overlayHeight / overlayTextureSize.cy));
	}
	if (overlayHeight == 0) {
		overlayHeight = (LONG)(overlayTextureSize.cy * ((FLOAT)overlayWidth / overlayTextureSize.cx));
	}
	LONG overlayPositionX = (pOverlay->Offset.value_or(SIZE{ 0 })).cx;
	LONG overlayPositionY = (pOverlay->Offset.value_or(SIZE{ 0 })).cy;

	LONG overlayLeft = 0;
	LONG overlayTop = 0;

	switch (pOverlay->Anchor)
	{
		case OverlayAnchor::TopLeft: {
			overlayLeft = overlayPositionX;
			overlayTop = overlayPositionY;
			break;
		}
		case OverlayAnchor::TopRight: {
			overlayLeft = backgroundWidth - overlayWidth - overlayPositionX;
			overlayTop = overlayPositionY;
			break;
		}
		case OverlayAnchor::BottomLeft: {
			overlayLeft = overlayPositionX;
			overlayTop = backgroundHeight - overlayHeight - overlayPositionY;
			break;
		}
		case OverlayAnchor::BottomRight: {
			overlayLeft = backgroundWidth - overlayWidth - overlayPositionX;
			overlayTop = backgroundHeight - overlayHeight - overlayPositionY;
			break;
		}
		default:
			break;
	}
	return RECT{ overlayLeft,overlayTop,overlayLeft + overlayWidth,overlayTop + overlayHeight };
}

//
// Clean up resources
//
void OverlayManager::Clean()
{
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
				delete m_OverlayThreadData[i].RecordingOverlay;
				m_OverlayThreadData[i].RecordingOverlay = nullptr;
			}
		}
		delete[] m_OverlayThreadData;
		m_OverlayThreadData = nullptr;
	}

	m_OverlayThreadCount = 0;

	CloseHandle(m_TerminateThreadsEvent);
}

void OverlayManager::WaitForThreadTermination()
{
	if (m_OverlayThreadCount != 0)
	{
		WaitForMultipleObjectsEx(m_OverlayThreadCount, m_OverlayThreadHandles, TRUE, INFINITE, FALSE);
	}
}

DWORD WINAPI OverlayCaptureThreadProc(_In_ void *Param) {
	HRESULT hr = S_OK;
	// Data passed in from thread creation
	OVERLAY_THREAD_DATA *pData = reinterpret_cast<OVERLAY_THREAD_DATA *>(Param);

	bool isExpectedError = false;
	bool isUnexpectedError = false;
	RECORDING_OVERLAY_DATA *pOverlay = pData->RecordingOverlay;
	unique_ptr<CaptureBase> overlayCapture = nullptr;
	switch (pOverlay->Type)
	{
		case RecordingSourceType::Picture: {
			std::string signature = ReadFileSignature(pData->RecordingOverlay->SourcePath.c_str());
			ImageFileType imageType = getImageTypeByMagic(signature.c_str());
			if (imageType == ImageFileType::IMAGE_FILE_GIF) {
				overlayCapture = make_unique<GifReader>();
			}
			else {
				overlayCapture = make_unique<ImageReader>();
			}
			break;
		}
		case RecordingSourceType::Video: {
			overlayCapture = make_unique<VideoReader>();
			break;
		}
		case RecordingSourceType::CameraCapture: {
			overlayCapture = make_unique<CameraCapture>();
			break;
		}
		case RecordingSourceType::Display: {
			overlayCapture = make_unique<WindowsGraphicsCapture>();
			break;
		}
		case RecordingSourceType::Window: {
			overlayCapture = make_unique<WindowsGraphicsCapture>();
			break;
		}
		default:
			break;
	}

	hr = overlayCapture->Initialize(pOverlay->DxRes.Context, pOverlay->DxRes.Device);
	hr = overlayCapture->StartCapture(*pOverlay);
	if (FAILED(hr))
	{
		return hr;
	}
	// D3D objects
	CComPtr<ID3D11Texture2D> SharedSurf = nullptr;
	CComPtr<IDXGIKeyedMutex> KeyMutex = nullptr;
	// Obtain handle to sync shared Surface
	hr = pOverlay->DxRes.Device->OpenSharedResource(pData->CanvasTexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&SharedSurf));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Opening shared texture failed");
		return hr;
	}
	hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&KeyMutex));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to get keyed mutex interface in spawned thread");
		return hr;
	}
	CComPtr<ID3D11Texture2D> pSharedTexture = nullptr;

	CComPtr<ID3D11Texture2D> pCurrentFrame = nullptr;
	bool WaitToProcessCurrentFrame = false;
	// Main capture loop
	while (true)
	{
		if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
			hr = S_OK;
			break;
		}

		if (!WaitToProcessCurrentFrame)
		{
			pCurrentFrame.Release();
			// Get new frame from video capture
			hr = overlayCapture->AcquireNextFrame(10, &pCurrentFrame);
			if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
				continue;
			}
			else if (FAILED(hr)) {
				break;
			}
		}

		// Try to acquire keyed mutex in order to access shared surface. The timeout value is 0, and we just continue if we don't get a lock.
		// This is just used to notify the rendering loop about updated overlays, so no reason to wait around if it's already updating.
		hr = KeyMutex->AcquireSync(0, 0);

		bool haveLock = SUCCEEDED(hr);
		if (haveLock) {
			LOG_TRACE(L"OverlayCapture %ls acquired mutex lock", overlayCapture->Name().c_str());
		}

		WaitToProcessCurrentFrame = false;
		if (pSharedTexture == nullptr) {
			D3D11_TEXTURE2D_DESC desc;
			pCurrentFrame->GetDesc(&desc);
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
			pOverlay->DxRes.Device->CreateTexture2D(&desc, nullptr, &pSharedTexture);
			HANDLE sharedHandle = GetSharedHandle(pSharedTexture);
			pData->OverlayTexSharedHandle = sharedHandle;
		}
		pOverlay->DxRes.Context->CopyResource(pSharedTexture, pCurrentFrame);
		//If a shared texture is updated on one device ID3D11DeviceContext::Flush must be called on that device. 
		//https://docs.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-opensharedresource
		pOverlay->DxRes.Context->Flush();
		QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
		if (haveLock) {
			KeyMutex->ReleaseSync(1);
		}
	}

	pData->ThreadResult = hr;
	if (isExpectedError) {
		SetEvent(pData->ExpectedErrorEvent);
	}
	else if (isUnexpectedError) {
		SetEvent(pData->UnexpectedErrorEvent);
	}
	return 0;
}
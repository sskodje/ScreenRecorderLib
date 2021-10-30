#include "Cleanup.h"
#include <chrono>
#include "ScreenCaptureManager.h"
#include "DesktopDuplicationCapture.h"
#include "WindowsGraphicsCapture.h"
#include "CameraCapture.h"
#include "VideoReader.h"
#include "ImageReader.h"
#include "GifReader.h"
#include <typeinfo>
using namespace DirectX;
using namespace std::chrono;
using namespace std;
DWORD WINAPI CaptureThreadProc(_In_ void *Param);
DWORD WINAPI OverlayCaptureThreadProc(_In_ void *Param);

ScreenCaptureManager::ScreenCaptureManager() :
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_TerminateThreadsEvent(nullptr),
	m_LastAcquiredFrameTimeStamp{},
	m_OutputRect{},
	m_SharedSurf(nullptr),
	m_KeyMutex(nullptr),
	m_CaptureThreadCount(0),
	m_CaptureThreadHandles(nullptr),
	m_CaptureThreadData(nullptr),
	m_OverlayThreadCount(0),
	m_OverlayThreadHandles(nullptr),
	m_OverlayThreadData(nullptr),
	m_TextureManager(nullptr),
	m_IsCapturing(false)
{
	RtlZeroMemory(&m_PtrInfo, sizeof(m_PtrInfo));
	// Event to tell spawned threads to quit
	m_TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

ScreenCaptureManager::~ScreenCaptureManager()
{
	if (m_IsCapturing) {
		StopCapture();
	}
	Clean();
}

//
// Initialize shaders for drawing to screen
//
HRESULT ScreenCaptureManager::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	HRESULT hr = S_OK;
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_TextureManager = make_unique<TextureManager>();
	RETURN_ON_BAD_HR(hr = m_TextureManager->Initialize(m_DeviceContext, m_Device));
	return hr;
}

//
// Start up threads for video capture
//
HRESULT ScreenCaptureManager::StartCapture(_In_ const std::vector<RECORDING_SOURCE *> &sources, _In_ const std::vector<RECORDING_OVERLAY *> &overlays, _In_  HANDLE hErrorEvent, _Inout_ CAPTURE_RESULT *pResult)
{
	ResetEvent(m_TerminateThreadsEvent);

	HRESULT hr = E_FAIL;
	std::vector<RECORDING_SOURCE_DATA *> CreatedOutputs{};
	RETURN_ON_BAD_HR(hr = CreateSharedSurf(sources, &CreatedOutputs, &m_OutputRect));
	m_CaptureThreadCount = (UINT)(CreatedOutputs.size());
	m_CaptureThreadHandles = new (std::nothrow) HANDLE[m_CaptureThreadCount]{};
	m_CaptureThreadData = new (std::nothrow) CAPTURE_THREAD_DATA[m_CaptureThreadCount]{};
	if (!m_CaptureThreadHandles || !m_CaptureThreadData)
	{
		return E_OUTOFMEMORY;
	}

	HANDLE sharedHandle = GetSharedHandle(m_SharedSurf);
	// Create appropriate # of threads for duplication

	for (UINT i = 0; i < m_CaptureThreadCount; i++)
	{
		RECORDING_SOURCE_DATA *data = CreatedOutputs.at(i);
		m_CaptureThreadData[i].ThreadResult = pResult;
		m_CaptureThreadData[i].ErrorEvent = hErrorEvent;
		m_CaptureThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_CaptureThreadData[i].CanvasTexSharedHandle = sharedHandle;
		m_CaptureThreadData[i].PtrInfo = &m_PtrInfo;

		m_CaptureThreadData[i].RecordingSource = data;
		RtlZeroMemory(&m_CaptureThreadData[i].RecordingSource->DxRes, sizeof(DX_RESOURCES));
		RETURN_ON_BAD_HR(hr = InitializeDx(nullptr, &m_CaptureThreadData[i].RecordingSource->DxRes));

		DWORD ThreadId;
		m_CaptureThreadHandles[i] = CreateThread(nullptr, 0, CaptureThreadProc, &m_CaptureThreadData[i], 0, &ThreadId);
		if (m_CaptureThreadHandles[i] == nullptr)
		{
			return E_FAIL;
		}
	}
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
		m_OverlayThreadData[i].ThreadResult = pResult;
		m_OverlayThreadData[i].ErrorEvent = hErrorEvent;
		m_OverlayThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_OverlayThreadData[i].CanvasTexSharedHandle = sharedHandle;
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

HRESULT ScreenCaptureManager::StopCapture()
{
	if (!SetEvent(m_TerminateThreadsEvent)) {
		LOG_ERROR("Could not terminate capture thread");
		return E_FAIL;
	}
	WaitForThreadTermination();
	m_IsCapturing = false;
	return S_OK;
}

HRESULT ScreenCaptureManager::AcquireNextFrame(_In_  DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame)
{
	HRESULT hr;
	// Try to acquire keyed mutex in order to access shared surface
	{
		MeasureExecutionTime measure(L"AcquireNextFrame wait for sync");
		hr = m_KeyMutex->AcquireSync(1, timeoutMillis);

	}
	if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
	{
		return DXGI_ERROR_WAIT_TIMEOUT;
	}
	else if (FAILED(hr))
	{
		return hr;
	}

	ID3D11Texture2D *pDesktopFrame = nullptr;
	{
		ReleaseKeyedMutexOnExit releaseMutex(m_KeyMutex, 0);

		if (!IsInitialFrameWriteComplete()) {
			return DXGI_ERROR_WAIT_TIMEOUT;
		}
		if (!IsUpdatedFramesAvailable()) {
			return DXGI_ERROR_WAIT_TIMEOUT;
		}
		MeasureExecutionTime measure(L"AcquireNextFrame lock");
		int updatedFrameCount = GetUpdatedFrameCount(true);

		D3D11_TEXTURE2D_DESC desc;
		m_SharedSurf->GetDesc(&desc);
		desc.MiscFlags = 0;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pDesktopFrame));
		m_DeviceContext->CopyResource(pDesktopFrame, m_SharedSurf);

		int updatedOverlaysCount = 0;
		ProcessOverlays(pDesktopFrame, &updatedOverlaysCount);

		if (updatedFrameCount > 0 || updatedOverlaysCount > 0) {
			QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);
		}

		pFrame->Frame = pDesktopFrame;
		pFrame->PtrInfo = &m_PtrInfo;
		pFrame->FrameUpdateCount = updatedFrameCount;
		pFrame->OverlayUpdateCount = updatedOverlaysCount;
	}
	return hr;
}

//
// Clean up resources
//
void ScreenCaptureManager::Clean()
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

//
// Waits infinitely for all spawned threads to terminate
//
void ScreenCaptureManager::WaitForThreadTermination()
{
	if (m_OverlayThreadCount != 0)
	{
		WaitForMultipleObjectsEx(m_OverlayThreadCount, m_OverlayThreadHandles, TRUE, INFINITE, FALSE);
	}
	if (m_CaptureThreadCount != 0) {
		WaitForMultipleObjectsEx(m_CaptureThreadCount, m_CaptureThreadHandles, TRUE, INFINITE, FALSE);
	}
}

_Ret_maybenull_ CAPTURE_THREAD_DATA *ScreenCaptureManager::GetCaptureDataForRect(RECT rect)
{
	POINT pt{ rect.left,rect.top };
	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		if (m_CaptureThreadData[i].RecordingSource) {
			if (PtInRect(&m_CaptureThreadData[i].RecordingSource->FrameCoordinates, pt)) {
				return &m_CaptureThreadData[i];
			}
		}
	}
	return nullptr;
}

RECT ScreenCaptureManager::GetSourceRect(_In_ SIZE canvasSize, _In_ RECORDING_SOURCE_DATA *pSource)
{
	int left = pSource->FrameCoordinates.left + pSource->OffsetX;
	int top = pSource->FrameCoordinates.top + pSource->OffsetY;
	return RECT{ left, top, left + RectWidth(pSource->FrameCoordinates),top + RectHeight(pSource->FrameCoordinates) };
}

bool ScreenCaptureManager::IsUpdatedFramesAvailable()
{
	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		if (m_CaptureThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			return true;
		}
	}
	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			return true;
		}
	}
	return false;
}

bool ScreenCaptureManager::IsInitialFrameWriteComplete()
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

bool ScreenCaptureManager::IsInitialOverlayWriteComplete()
{
	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].RecordingOverlay) {
			if (!FAILED(m_OverlayThreadData[i].ThreadResult) && m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart == 0) {
				//If any of the overlays have not yet written a frame, we return and wait for them.
				return false;
			}
		}
	}
	return true;
}

UINT ScreenCaptureManager::GetUpdatedFrameCount(_In_ bool resetUpdatedFrameCounts)
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

RECT ScreenCaptureManager::GetOverlayRect(_In_ SIZE canvasSize, _In_ SIZE overlayTextureSize, _In_ RECORDING_OVERLAY *pOverlay)
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
		case ContentAnchor::TopLeft: {
			overlayLeft = overlayPositionX;
			overlayTop = overlayPositionY;
			break;
		}
		case ContentAnchor::TopRight: {
			overlayLeft = backgroundWidth - overlayWidth - overlayPositionX;
			overlayTop = overlayPositionY;
			break;
		}
		case ContentAnchor::Center: {
			overlayLeft = (backgroundWidth / 2) - (overlayWidth / 2) + overlayPositionX;
			overlayTop = (backgroundHeight / 2) - (overlayHeight / 2) + overlayPositionY;
			break;
		}
		case ContentAnchor::BottomLeft: {
			overlayLeft = overlayPositionX;
			overlayTop = backgroundHeight - overlayHeight - overlayPositionY;
			break;
		}
		case ContentAnchor::BottomRight: {
			overlayLeft = backgroundWidth - overlayWidth - overlayPositionX;
			overlayTop = backgroundHeight - overlayHeight - overlayPositionY;
			break;
		}
		default:
			break;
	}
	return RECT{ overlayLeft,overlayTop,overlayLeft + overlayWidth,overlayTop + overlayHeight };
}

HRESULT ScreenCaptureManager::ProcessOverlays(_Inout_ ID3D11Texture2D *pCanvasTexture, _Out_ int *updateCount)
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
				CONTINUE_ON_BAD_HR(hr = m_TextureManager->DrawTexture(pCanvasTexture, pOverlayTexture, GetOverlayRect(canvasSize, textureSize, pOverlayData->RecordingOverlay)));
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

HRESULT ScreenCaptureManager::CreateSharedSurf(_In_ const std::vector<RECORDING_SOURCE *> &sources, _Out_ std::vector<RECORDING_SOURCE_DATA *> *pCreatedOutputs, _Out_ RECT *pDeskBounds)
{
	*pCreatedOutputs = std::vector<RECORDING_SOURCE_DATA *>();
	std::vector<std::pair<RECORDING_SOURCE *, RECT>> validOutputs;
	HRESULT hr = GetOutputRectsForRecordingSources(sources, &validOutputs);
	if (FAILED(hr)) {
		LOG_ERROR(L"Failed to calculate output rects for recording sources");
		return hr;
	}

	std::vector<RECT> outputRects{};
	for each (auto & pair in validOutputs)
	{
		outputRects.push_back(pair.second);
	}
	std::vector<SIZE> outputOffsets{};
	GetCombinedRects(outputRects, pDeskBounds, &outputOffsets);

	pDeskBounds = &MakeRectEven(*pDeskBounds);
	for (int i = 0; i < validOutputs.size(); i++)
	{
		RECORDING_SOURCE *source = validOutputs.at(i).first;
		RECT sourceRect = validOutputs.at(i).second;
		RECORDING_SOURCE_DATA *data = new RECORDING_SOURCE_DATA(source);

		data->OffsetX -= pDeskBounds->left + outputOffsets.at(i).cx;
		data->OffsetY -= pDeskBounds->top + outputOffsets.at(i).cy;
		data->FrameCoordinates = sourceRect;
		pCreatedOutputs->push_back(data);
	}

	// Set created outputs
	hr = ScreenCaptureManager::CreateSharedSurf(*pDeskBounds, &m_SharedSurf, &m_KeyMutex);
	return hr;
}

HRESULT ScreenCaptureManager::CreateSharedSurf(_In_ RECT desktopRect, _Outptr_ ID3D11Texture2D **ppSharedTexture, _Outptr_ IDXGIKeyedMutex **ppKeyedMutex)
{
	CComPtr<ID3D11Texture2D> pSharedTexture = nullptr;
	CComPtr<IDXGIKeyedMutex> pKeyedMutex = nullptr;
	// Create shared texture for all capture threads to draw into
	D3D11_TEXTURE2D_DESC DeskTexD;
	RtlZeroMemory(&DeskTexD, sizeof(D3D11_TEXTURE2D_DESC));
	DeskTexD.Width = RectWidth(desktopRect);
	DeskTexD.Height = RectHeight(desktopRect);
	DeskTexD.MipLevels = 1;
	DeskTexD.ArraySize = 1;
	DeskTexD.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	DeskTexD.SampleDesc.Count = 1;
	DeskTexD.Usage = D3D11_USAGE_DEFAULT;
	DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET;
	DeskTexD.CPUAccessFlags = 0;
	DeskTexD.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

	HRESULT hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &pSharedTexture);
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to create shared texture");
		return hr;
	}
	// Get keyed mutex
	hr = pSharedTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&pKeyedMutex));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to query for keyed mutex in OUTPUTMANAGER");
		return hr;
	}
	if (ppSharedTexture) {
		*ppSharedTexture = pSharedTexture;
		(*ppSharedTexture)->AddRef();
	}
	if (ppKeyedMutex) {
		*ppKeyedMutex = pKeyedMutex;
		(*ppKeyedMutex)->AddRef();
	}
	return hr;
}


DWORD WINAPI CaptureThreadProc(_In_ void *Param)
{
	HRESULT hr = E_FAIL;
	// D3D objects
	CComPtr<ID3D11Texture2D> SharedSurf = nullptr;
	CComPtr<IDXGIKeyedMutex> KeyMutex = nullptr;

	// Data passed in from thread creation
	CAPTURE_THREAD_DATA *pData = reinterpret_cast<CAPTURE_THREAD_DATA *>(Param);
	RECORDING_SOURCE_DATA *pSourceData = pData->RecordingSource;
	RECORDING_SOURCE *pSource = pSourceData->RecordingSource;
	//This scope must be here for ReleaseOnExit to work.
	{
		std::unique_ptr<CaptureBase> pRecordingSourceCapture = nullptr;
		switch (pSource->Type)
		{
			case RecordingSourceType::CameraCapture: {
				pRecordingSourceCapture = make_unique<CameraCapture>();
				break;
			}
			case RecordingSourceType::Display: {
				if (pSource->SourceApi == RecordingSourceApi::DesktopDuplication) {
					pRecordingSourceCapture = make_unique<DesktopDuplicationCapture>(pSource->IsCursorCaptureEnabled.value_or(false));
				}
				else if (pSource->SourceApi == RecordingSourceApi::WindowsGraphicsCapture) {
					pRecordingSourceCapture = make_unique<WindowsGraphicsCapture>(pSource->IsCursorCaptureEnabled.value_or(false));
				}
				break;
			}
			case RecordingSourceType::Picture: {
				std::string signature = ReadFileSignature(pSource->SourcePath.c_str());
				ImageFileType imageType = getImageTypeByMagic(signature.c_str());
				if (imageType == ImageFileType::IMAGE_FILE_GIF) {
					pRecordingSourceCapture = make_unique<GifReader>();
				}
				else {
					pRecordingSourceCapture = make_unique<ImageReader>();
				}
				break;
			}
			case RecordingSourceType::Video: {
				pRecordingSourceCapture = make_unique<VideoReader>();
				break;
			}
			case RecordingSourceType::Window: {
				pRecordingSourceCapture = make_unique<WindowsGraphicsCapture>();
				break;
			}
			default:
				break;
		}

		if (!pRecordingSourceCapture) {
			LOG_ERROR(L"Failed to create recording source");
			hr = E_FAIL;
			goto Exit;
		}

		// Obtain handle to sync shared Surface
		hr = pSourceData->DxRes.Device->OpenSharedResource(pData->CanvasTexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&SharedSurf));
		if (FAILED(hr))
		{
			LOG_ERROR(L"Opening shared texture failed");
			goto Exit;
		}
		hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&KeyMutex));
		if (FAILED(hr))
		{
			LOG_ERROR(L"Failed to get keyed mutex interface in spawned thread");
			goto Exit;
		}

		// Make duplication
		hr = pRecordingSourceCapture->Initialize(pSourceData->DxRes.Context, pSourceData->DxRes.Device);

		if (FAILED(hr))
		{
			LOG_ERROR(L"Failed to initialize recording source");
			goto Exit;
		}
		hr = pRecordingSourceCapture->StartCapture(*pSource);

		if (FAILED(hr))
		{
			LOG_ERROR(L"Failed to start capture");
			goto Exit;
		}

		// Main duplication loop
		bool WaitToProcessCurrentFrame = false;
		while (true)
		{
			if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
				hr = S_OK;
				break;
			}

			if (!WaitToProcessCurrentFrame)
			{
				hr = pRecordingSourceCapture->AcquireNextFrame(10, nullptr);

				if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
					continue;
				}
				else if (FAILED(hr)) {
					break;
				}
			}
			{
				MeasureExecutionTime measure(L"CaptureThreadProc wait for sync");
				// We have a new frame so try and process it
				// Try to acquire keyed mutex in order to access shared surface
				hr = KeyMutex->AcquireSync(0, 1);
			}
			if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
			{
				LOG_TRACE(L"CaptureThreadProc shared surface is busy for %ls, retrying..", pRecordingSourceCapture->Name().c_str());
				// Can't use shared surface right now, try again later
				WaitToProcessCurrentFrame = true;
				continue;
			}
			else if (FAILED(hr))
			{
				// Generic unknown failure
				LOG_ERROR(L"Unexpected error acquiring KeyMutex");
				break;
			}
			{
				MeasureExecutionTime measureLock(string_format(L"CaptureThreadProc sync lock for %ls", pRecordingSourceCapture->Name().c_str()));
				ReleaseKeyedMutexOnExit releaseMutex(KeyMutex, 1);

				// We can now process the current frame
				WaitToProcessCurrentFrame = false;

				if (pSource->IsCursorCaptureEnabled.value_or(false)) {
					// Get mouse info
					hr = pRecordingSourceCapture->GetMouse(pData->PtrInfo, pSourceData->FrameCoordinates, pSourceData->OffsetX, pSourceData->OffsetY);
					if (FAILED(hr)) {
						LOG_ERROR("Failed to get mouse data");
					}
				}

				hr = pRecordingSourceCapture->WriteNextFrameToSharedSurface(0, SharedSurf, pSourceData->OffsetX, pSourceData->OffsetY, pSourceData->FrameCoordinates);
			}
			if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
				continue;
			}
			else if (FAILED(hr)) {
				break;
			}
			else if (hr == S_FALSE) {
				continue;
			}

			pData->UpdatedFrameCountSinceLastWrite++;
			pData->TotalUpdatedFrameCount++;
			QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
		}
	}
Exit:
	if (pData->ThreadResult) {
		pData->ThreadResult->RecordingResult = hr;

		if (FAILED(hr))
		{
			//E_ABORT is returned when the capture loop should be stopped, but the recording continue. On other errors, we check how to handle them.
			if (hr != E_ABORT) {
				ProcessCaptureHRESULT(hr, pData->ThreadResult, pSourceData->DxRes.Device);
				if (pData->ThreadResult->IsRecoverableError) {
					LOG_INFO("Recoverable error in capture, reinitializing..");
				}
				else {
					LOG_ERROR("Fatal error in capture, exiting..");
				}
				SetEvent(pData->ErrorEvent);
			}
		}
	}
	LOG_DEBUG("Exiting CaptureThreadProc");
	return 0;
}


DWORD WINAPI OverlayCaptureThreadProc(_In_ void *Param) {
	HRESULT hr = E_FAIL;
	// Data passed in from thread creation
	OVERLAY_THREAD_DATA *pData = reinterpret_cast<OVERLAY_THREAD_DATA *>(Param);

	RECORDING_OVERLAY_DATA *pOverlayData = pData->RecordingOverlay;
	RECORDING_OVERLAY *pOverlay = pOverlayData->RecordingOverlay;
	unique_ptr<CaptureBase> overlayCapture = nullptr;

	// D3D objects
	CComPtr<ID3D11Texture2D> pSharedTexture = nullptr;
	CComPtr<ID3D11Texture2D> pCurrentFrame = nullptr;
	CComPtr<ID3D11Texture2D> SharedSurf = nullptr;
	CComPtr<IDXGIKeyedMutex> KeyMutex = nullptr;

	switch (pOverlay->Type)
	{
		case RecordingSourceType::Picture: {
			std::string signature = ReadFileSignature(pOverlay->SourcePath.c_str());
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

	if (!overlayCapture) {
		LOG_ERROR(L"Failed to create recording source");
		goto Exit;
	}

	hr = overlayCapture->Initialize(pOverlayData->DxRes.Context, pOverlayData->DxRes.Device);
	hr = overlayCapture->StartCapture(*pOverlay);
	if (FAILED(hr))
	{
		goto Exit;
	}

	// Obtain handle to sync shared Surface
	hr = pOverlayData->DxRes.Device->OpenSharedResource(pData->CanvasTexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&SharedSurf));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Opening shared texture failed");
		goto Exit;
	}
	hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&KeyMutex));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to get keyed mutex interface in spawned thread");
		goto Exit;
	}

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
			pOverlayData->DxRes.Device->CreateTexture2D(&desc, nullptr, &pSharedTexture);
			HANDLE sharedHandle = GetSharedHandle(pSharedTexture);
			pData->OverlayTexSharedHandle = sharedHandle;
		}
		pOverlayData->DxRes.Context->CopyResource(pSharedTexture, pCurrentFrame);
		//If a shared texture is updated on one device ID3D11DeviceContext::Flush must be called on that device. 
		//https://docs.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-opensharedresource
		pOverlayData->DxRes.Context->Flush();
		QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
		// Try to acquire keyed mutex in order to access shared surface. The timeout value is 0, and we just continue if we don't get a lock.
		// This is just used to notify the rendering loop about updated overlays, so no reason to wait around if it's already updating.
		if (KeyMutex->AcquireSync(0, 0) == S_OK) {
			MeasureExecutionTime measureLock(string_format(L"OverlayCapture sync lock for %ls", overlayCapture->Name().c_str()));
			KeyMutex->ReleaseSync(1);
		}
	}
Exit:
	if (pData->ThreadResult) {
		pData->ThreadResult->RecordingResult = hr;

		if (FAILED(hr))
		{
			//E_ABORT is returned when the capture loop should be stopped, but the recording continue. On other errors, we check how to handle them.
			if (hr != E_ABORT) {
				ProcessCaptureHRESULT(hr, pData->ThreadResult, pOverlayData->DxRes.Device);
				if (pData->ThreadResult->IsRecoverableError) {
					LOG_INFO("Recoverable error in capture, reinitializing..");
				}
				else {
					LOG_ERROR("Fatal error in capture, exiting..");
				}
				SetEvent(pData->ErrorEvent);
			}
		}
	}
	LOG_DEBUG("Exiting OverlayCaptureThreadProc");
	return 0;
}

void ProcessCaptureHRESULT(_In_ HRESULT hr, _Inout_ CAPTURE_RESULT *pResult, _In_opt_ ID3D11Device *pDevice) {
	pResult->RecordingResult = hr;
	_com_error err(hr);
	switch (hr)
	{
		case DXGI_ERROR_DEVICE_REMOVED:
		{
			if (pDevice) {
				HRESULT DeviceRemovedReason = pDevice->GetDeviceRemovedReason();

				switch (DeviceRemovedReason)
				{
					case DXGI_ERROR_DEVICE_REMOVED:
					case DXGI_ERROR_DEVICE_RESET:
					case static_cast<HRESULT>(E_OUTOFMEMORY):
					{
						LOG_INFO(L"Graphics device temporarily unavailable: hr = 0x%08x, error = %s", hr, err.ErrorMessage());
						pResult->IsRecoverableError = true;
						break;
					}
					default: {
						LOG_ERROR(L"Graphics device unavailable: hr = 0x%08x, error = %s", hr, err.ErrorMessage());
					}
				}
			}
			break;
		}
		case static_cast<HRESULT>(E_ACCESSDENIED):
		case DXGI_ERROR_MODE_CHANGE_IN_PROGRESS:
		case DXGI_ERROR_SESSION_DISCONNECTED:
		case DXGI_ERROR_ACCESS_LOST:
			//Access to video output is denied, probably due to DRM, screen saver, desktop is switching, fullscreen application is launching, or similar.
			//We continue the recording, and instead of desktop texture just add a blank texture instead.
			pResult->IsRecoverableError = true;
			pResult->Error = L"Desktop temporarily unavailable";
			LOG_INFO(L"Desktop temporarily unavailable: hr = 0x%08x, error = %s", hr, err.ErrorMessage());
			break;
		case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
			pResult->Error = L"Desktop Duplication for output is currently unavailable.";
			LOG_ERROR(L"Error in capture with DXGI_ERROR_NOT_CURRENTLY_AVAILABLE. This probably means DXGI reached the limit on the maximum number of concurrent duplication applications (default of four). Therefore, the calling application cannot create any desktop duplication interfaces until the other applications close");
			break;
		default:
			//Unexpected error, return.
			LOG_ERROR(L"Unexpected error, aborting capture: %s", err.ErrorMessage());
			pResult->Error = L"Unexpected error, aborting capture";
			break;
	}
}
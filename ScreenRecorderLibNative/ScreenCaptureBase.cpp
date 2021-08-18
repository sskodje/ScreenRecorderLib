#include "CameraCapture.h"
#include "Cleanup.h"
#include "VideoReader.h"
#include "GifReader.h"
#include <chrono>

using namespace DirectX;
using namespace std::chrono;
using namespace std;

DWORD WINAPI OverlayProc(_In_ void *Param);
HRESULT ReadMedia(CaptureBase &reader, OVERLAY_THREAD_DATA *pData);
HRESULT ReadImage(OVERLAY_THREAD_DATA *pData);

ScreenCaptureBase::ScreenCaptureBase() :
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
	m_CaptureThreadData(nullptr),
	m_TextureManager(nullptr)
{
	RtlZeroMemory(&m_PtrInfo, sizeof(m_PtrInfo));
	// Event to tell spawned threads to quit
	m_TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

ScreenCaptureBase::~ScreenCaptureBase()
{
	Clean();
}

//
// Initialize shaders for drawing to screen
//
HRESULT ScreenCaptureBase::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_TextureManager = make_unique<TextureManager>();
	HRESULT hr = m_TextureManager->Initialize(m_DeviceContext, m_Device);

	return hr;
}

//
// Start up threads for DDA
//
HRESULT ScreenCaptureBase::StartCapture(_In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent)
{
	ResetEvent(m_TerminateThreadsEvent);

	HRESULT hr;
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
		m_CaptureThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_CaptureThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_CaptureThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_CaptureThreadData[i].TexSharedHandle = sharedHandle;
		m_CaptureThreadData[i].PtrInfo = &m_PtrInfo;

		m_CaptureThreadData[i].RecordingSource = data;
		RtlZeroMemory(&m_CaptureThreadData[i].RecordingSource->DxRes, sizeof(DX_RESOURCES));
		m_CaptureThreadData[i].RecordingSource->DxRes.Context = m_DeviceContext;
		m_CaptureThreadData[i].RecordingSource->DxRes.Device = m_Device;
		RETURN_ON_BAD_HR(hr = InitializeDx(nullptr, &m_CaptureThreadData[i].RecordingSource->DxRes));

		DWORD ThreadId;
		m_CaptureThreadHandles[i] = CreateThread(nullptr, 0, GetCaptureThreadProc(), &m_CaptureThreadData[i], 0, &ThreadId);
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
		m_OverlayThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_OverlayThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_OverlayThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_OverlayThreadData[i].TexSharedHandle = sharedHandle;
		m_OverlayThreadData[i].RecordingOverlay = new RECORDING_OVERLAY_DATA(overlay);
		RtlZeroMemory(&m_OverlayThreadData[i].RecordingOverlay->DxRes, sizeof(DX_RESOURCES));
		m_OverlayThreadData[i].RecordingOverlay->DxRes.Context = m_DeviceContext;
		m_OverlayThreadData[i].RecordingOverlay->DxRes.Device = m_Device;
		HRESULT hr = InitializeDx(nullptr, &m_OverlayThreadData[i].RecordingOverlay->DxRes);
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
	}
	m_IsCapturing = true;
	return hr;
}

HRESULT ScreenCaptureBase::StopCapture()
{
	if (!SetEvent(m_TerminateThreadsEvent)) {
		LOG_ERROR("Could not terminate capture thread");
		return E_FAIL;
	}
	WaitForThreadTermination();
	m_IsCapturing = false;
	return S_OK;
}

HRESULT ScreenCaptureBase::AcquireNextFrame(_In_  DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame)
{
	HRESULT hr;
	// Try to acquire keyed mutex in order to access shared surface
	{
		MeasureExecutionTime measure(L"AcquireNextFrame wait for sync");
		hr = m_KeyMutex->AcquireSync(1, timeoutMillis);

	}
	if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
	//if (WaitForSingleObject(m_Mutex, timeoutMillis) != WAIT_OBJECT_0)
	{
		return DXGI_ERROR_WAIT_TIMEOUT;
	}
	else if (FAILED(hr))
	{
		return hr;
	}

	ID3D11Texture2D *pDesktopFrame = nullptr;
	{
		//ReleaseMutexHandleOnExit releaseMutex(m_Mutex);
		ReleaseKeyedMutexOnExit releaseMutex(m_KeyMutex, 0);

		bool haveNewFrameData = IsUpdatedFramesAvailable() && IsInitialFrameWriteComplete();
		if (!haveNewFrameData) {
			LOG_TRACE("No new frames available");
			return DXGI_ERROR_WAIT_TIMEOUT;
		}
		MeasureExecutionTime measure(L"AcquireNextFrame lock");
		int updatedFrameCount = GetUpdatedFrameCount(true);
		//{
		//	MeasureExecutionTime measure(L"ProcessSources");
		//	RETURN_ON_BAD_HR(hr = ProcessSources(m_SharedSurf, &updatedFrameCount));
		//	measure.SetName(string_format(L"ProcessSources for %d sources", updatedFrameCount));
		//}

		int updatedOverlaysCount = 0;
		//{
		//	MeasureExecutionTime measure(L"ProcessOverlays");
		//	RETURN_ON_BAD_HR(hr = ProcessOverlays(m_SharedSurf, &updatedOverlaysCount));
		//	measure.SetName(string_format(L"ProcessOverlays for %d sources", updatedOverlaysCount));
		//}

		D3D11_TEXTURE2D_DESC desc;
		m_SharedSurf->GetDesc(&desc);
		desc.MiscFlags = 0;
		RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pDesktopFrame));
		m_DeviceContext->CopyResource(pDesktopFrame, m_SharedSurf);

		//m_DeviceContext->CopyResource(m_SharedSurf, pDesktopFrame);
		if (updatedFrameCount > 0 || updatedOverlaysCount > 0) {
			QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);
		}

		pFrame->Frame = pDesktopFrame;
		pFrame->PtrInfo = &m_PtrInfo;
		pFrame->FrameUpdateCount = updatedFrameCount;
		//pFrame->Timestamp = m_LastAcquiredFrameTimeStamp;
		pFrame->OverlayUpdateCount = updatedOverlaysCount;
	}
	return hr;
}

//
// Clean up resources
//
void ScreenCaptureBase::Clean()
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
				//if (m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer) {
				//	delete[] m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer;
				//	m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer = nullptr;
				//}
				SafeRelease(&m_OverlayThreadData[i].RecordingOverlay->FrameInfo->Frame);
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
				//SafeRelease(&m_CaptureThreadData[i].FrameInfo->Frame);
				//delete m_CaptureThreadData[i].FrameInfo;
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
void ScreenCaptureBase::WaitForThreadTermination()
{
	if (m_OverlayThreadCount != 0)
	{
		WaitForMultipleObjectsEx(m_OverlayThreadCount, m_OverlayThreadHandles, TRUE, INFINITE, FALSE);
	}
	if (m_CaptureThreadCount != 0) {
		WaitForMultipleObjectsEx(m_CaptureThreadCount, m_CaptureThreadHandles, TRUE, INFINITE, FALSE);
	}
}

_Ret_maybenull_ CAPTURE_THREAD_DATA *ScreenCaptureBase::GetCaptureDataForRect(RECT rect)
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

RECT ScreenCaptureBase::GetSourceRect(_In_ SIZE canvasSize, _In_ RECORDING_SOURCE_DATA *pSource)
{
	int left = pSource->FrameCoordinates.left + pSource->OffsetX;
	int top = pSource->FrameCoordinates.top + pSource->OffsetY;
	return RECT{ left, top, left + RectWidth(pSource->FrameCoordinates),top + RectHeight(pSource->FrameCoordinates) };
}

RECT ScreenCaptureBase::GetOverlayRect(_In_ SIZE canvasSize, _In_ SIZE overlayTextureSize, _In_ RECORDING_OVERLAY_DATA *pOverlay)
{
	LONG backgroundWidth = canvasSize.cx;
	LONG backgroundHeight = canvasSize.cy;
	// Clipping adjusted coordinates / dimensions
	LONG overlayWidth = pOverlay->Size.cx;
	LONG overlayHeight = pOverlay->Size.cy;
	if (overlayWidth == 0 && overlayHeight == 0) {
		overlayWidth = overlayTextureSize.cx;
		overlayHeight = overlayTextureSize.cy;
	}
	if (overlayWidth == 0) {
		overlayWidth = (LONG)(overlayTextureSize.cx * ((FLOAT)overlayHeight / overlayTextureSize.cy));
	}
	if (overlayHeight == 0) {
		overlayHeight = (LONG)(overlayTextureSize.cx * ((FLOAT)overlayWidth / overlayTextureSize.cx));
	}
	LONG overlayLeft = 0;
	LONG overlayTop = 0;

	switch (pOverlay->Anchor)
	{
	case OverlayAnchor::TopLeft: {
		overlayLeft = pOverlay->Offset.x;
		overlayTop = pOverlay->Offset.y;
		break;
	}
	case OverlayAnchor::TopRight: {
		overlayLeft = backgroundWidth - overlayWidth - pOverlay->Offset.x;
		overlayTop = pOverlay->Offset.y;
		break;
	}
	case OverlayAnchor::BottomLeft: {
		overlayLeft = pOverlay->Offset.x;
		overlayTop = backgroundHeight - overlayHeight - pOverlay->Offset.y;
		break;
	}
	case OverlayAnchor::BottomRight: {
		overlayLeft = backgroundWidth - overlayWidth - pOverlay->Offset.x;
		overlayTop = backgroundHeight - overlayHeight - pOverlay->Offset.y;
		break;
	}
	default:
		break;
	}
	return RECT{ overlayLeft,overlayTop,overlayLeft + overlayWidth,overlayTop + overlayHeight };
}

//
// Returns shared handle
//
_Ret_maybenull_ HANDLE ScreenCaptureBase::GetSharedHandle(_In_ ID3D11Texture2D *pSurface)
{
	if (!pSurface) {
		return nullptr;
	}
	HANDLE Hnd = nullptr;

	// QI IDXGIResource interface to synchronized shared surface.
	IDXGIResource *DXGIResource = nullptr;
	HRESULT hr = pSurface->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&DXGIResource));
	if (SUCCEEDED(hr))
	{
		// Obtain handle to IDXGIResource object.
		DXGIResource->GetSharedHandle(&Hnd);
		DXGIResource->Release();
		DXGIResource = nullptr;
	}

	return Hnd;
}

bool ScreenCaptureBase::IsUpdatedFramesAvailable()
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

bool ScreenCaptureBase::IsInitialFrameWriteComplete()
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

UINT ScreenCaptureBase::GetUpdatedFrameCount(_In_ bool resetUpdatedFrameCounts)
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

HRESULT ScreenCaptureBase::CreateSharedSurf(_In_ std::vector<RECORDING_SOURCE> sources, _Out_ std::vector<RECORDING_SOURCE_DATA *> *pCreatedOutputs, _Out_ RECT *pDeskBounds)
{
	*pCreatedOutputs = std::vector<RECORDING_SOURCE_DATA *>();
	std::vector<std::pair<RECORDING_SOURCE, RECT>> validOutputs;
	HRESULT hr = GetOutputRectsForRecordingSources(sources, &validOutputs);
	if (FAILED(hr)) {
		LOG_ERROR(L"Failed to calculate output rects for recording sources");
		return hr;
	}

	std::vector<RECT> outputRects{};
	for each (auto pair in validOutputs)
	{
		outputRects.push_back(pair.second);
	}
	std::vector<SIZE> outputOffsets{};
	GetCombinedRects(outputRects, pDeskBounds, &outputOffsets);

	pDeskBounds = &MakeRectEven(*pDeskBounds);
	int accumulatedWindowXOffset = 0;
	for (int i = 0; i < validOutputs.size(); i++)
	{
		RECORDING_SOURCE source = validOutputs.at(i).first;
		RECT sourceRect = validOutputs.at(i).second;
		RECORDING_SOURCE_DATA *data = new RECORDING_SOURCE_DATA(source);

		if (source.Type == RecordingSourceType::Display) {
			data->OffsetX -= pDeskBounds->left;
			data->OffsetY -= pDeskBounds->top;

		}
		else if (source.Type == RecordingSourceType::Window) {

			accumulatedWindowXOffset += RectWidth(sourceRect);
		}
		data->OffsetX -= outputOffsets.at(i).cx;
		data->OffsetY -= outputOffsets.at(i).cy;
		data->FrameCoordinates = sourceRect;
		pCreatedOutputs->push_back(data);
	}

	// Set created outputs
	hr = ScreenCaptureBase::CreateSharedSurf(*pDeskBounds, &m_SharedSurf, &m_KeyMutex);
	return hr;
}

HRESULT ScreenCaptureBase::CreateSharedSurf(_In_ RECT desktopRect, _Outptr_ ID3D11Texture2D **ppSharedTexture, _Outptr_ IDXGIKeyedMutex **ppKeyedMutex)
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

//HRESULT ScreenCaptureBase::ProcessSources(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Out_ int *updateCount)
//{
//	HRESULT hr = S_FALSE;
//	int count = 0;
//	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
//	{
//		if (m_CaptureThreadData[i].RecordingSource) {
//			RECORDING_SOURCE_DATA *pSource = m_CaptureThreadData[i].RecordingSource;
//			FRAME_BASE *pFrameInfo = m_CaptureThreadData[i].FrameInfo;
//			if (pSource && pFrameInfo && pFrameInfo->Frame) {
//				
//
//				if (m_CaptureThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
//						hr = m_TextureManager->DrawTexture(pBackgroundFrame, pFrameInfo->Frame, GetSourceRect(GetOutputSize(), pSource));
//					count++;
//					//m_CaptureThreadData[i].UpdatedFrameCountSinceLastWrite = 0;
//				}
//			}
//		}
//	}
//	*updateCount = count;
//	return S_OK;
//}

//HRESULT ScreenCaptureBase::ProcessOverlays(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Out_ int *updateCount)
//{
//	HRESULT hr = S_FALSE;
//	int count = 0;
//	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
//	{
//		if (m_OverlayThreadData[i].RecordingOverlay) {
//			RECORDING_OVERLAY_DATA *pOverlayData = m_OverlayThreadData[i].RecordingOverlay;
//			FRAME_BASE *pFrameInfo = m_OverlayThreadData[i].FrameInfo;
//			if (pOverlayData && pFrameInfo && pFrameInfo->Frame) {
//				//DrawOverlay(pBackgroundFrame, pOverlay);
//				D3D11_TEXTURE2D_DESC desc;
//				pFrameInfo->Frame->GetDesc(&desc);
//				SIZE textureSize = SIZE{ (LONG)desc.Width,(LONG)desc.Height };
//				hr = m_TextureManager->DrawTexture(pBackgroundFrame, pFrameInfo->Frame, GetOverlayRect(GetOutputSize(), textureSize, pOverlayData));
//				if (m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
//					count++;
//				}
//			}
//		}
//	}
//	*updateCount = count;
//	return hr;
//}

//HRESULT ScreenCaptureBase::DrawSource(_Inout_ ID3D11Texture2D *pBackgroundFrame, _In_ RECORDING_SOURCE_DATA *pSource)
//{
//	FRAME_BASE *pFrameInfo = pSource->FrameInfo;
//	if (!pFrameInfo || pFrameInfo->Frame == nullptr)
//		return S_FALSE;
//
//	HRESULT hr = m_TextureManager->DrawTexture(pBackgroundFrame, pSource->FrameInfo->Frame, GetSourceRect(GetOutputSize(), pSource));
//	return hr;
//}
//
//HRESULT ScreenCaptureBase::DrawOverlay(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Inout_ ID3D11Texture2D *pOverlayFrame, _In_ RECORDING_OVERLAY_DATA *pOverlay)
//{
//	if (!pOverlayFrame || !pOverlay)
//		return S_FALSE;
//
//	HRESULT hr = m_TextureManager->DrawTexture(pBackgroundFrame, pOverlayFrame, GetOverlayRect(GetOutputSize(), pOverlay));
//	return hr;
//}

bool ScreenCaptureBase::IsSingleWindowCapture()
{
	return m_CaptureThreadCount == 1 && m_CaptureThreadData[0].RecordingSource->WindowHandle;
}

DWORD WINAPI OverlayProc(_In_ void *Param) {
	HRESULT hr = S_OK;
	// Data passed in from thread creation
	OVERLAY_THREAD_DATA *pData = reinterpret_cast<OVERLAY_THREAD_DATA *>(Param);

	bool isExpectedError = false;
	bool isUnexpectedError = false;
	RECORDING_OVERLAY_DATA *pOverlay = pData->RecordingOverlay;
	switch (pOverlay->Type)
	{
	case RecordingOverlayType::Picture: {
		std::string signature = ReadFileSignature(pOverlay->Source.c_str());
		ImageFileType imageType = getImageTypeByMagic(signature.c_str());
		if (imageType == ImageFileType::IMAGE_FILE_GIF) {
			GifReader gifReader{};
			hr = ReadMedia(gifReader, pData);
		}
		else {
			hr = ReadImage(pData);
		}
		break;
	}
	case RecordingOverlayType::Video: {
		VideoReader videoReader{};
		hr = ReadMedia(videoReader, pData);
		break;
	}
	case RecordingOverlayType::CameraCapture: {
		CameraCapture videoCapture{};
		hr = ReadMedia(videoCapture, pData);
		break;
	}
	default:
		break;
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

HRESULT ReadImage(OVERLAY_THREAD_DATA *pData) {
	RECORDING_OVERLAY_DATA *pOverlay = pData->RecordingOverlay;

	CComPtr<IWICBitmapSource> pBitmap;
	HRESULT hr = CreateWICBitmapFromFile(pOverlay->Source.c_str(), GUID_WICPixelFormat32bppBGRA, &pBitmap);
	if (FAILED(hr)) {
		return hr;
	}
	// Copy the 32bpp RGBA image to a buffer for further processing.
	UINT width, height;
	RETURN_ON_BAD_HR(hr = pBitmap->GetSize(&width, &height));

	const unsigned bytesPerPixel = 4;
	const unsigned stride = width * bytesPerPixel;
	const unsigned bitmapSize = width * height * bytesPerPixel;

	FRAME_BASE *pFrame = pOverlay->FrameInfo;
	//pFrame->BufferSize = bitmapSize;
	//pFrame->Stride = stride;
	//pFrame->Width = width;
	//pFrame->Height = height;
	//if (pFrame->PtrFrameBuffer)
	//{
	//	delete[] pFrame->PtrFrameBuffer;
	//	pFrame->PtrFrameBuffer = nullptr;
	//}
	//pFrame->PtrFrameBuffer = new (std::nothrow) BYTE[pFrame->BufferSize];
	//if (!pFrame->PtrFrameBuffer)
	//{
	//	pFrame->BufferSize = 0;
	//	LOG_ERROR(L"Failed to allocate memory for frame");
	//	return E_OUTOFMEMORY;
	//}
	BYTE *pFrameBuffer = new (std::nothrow) BYTE[bitmapSize];
	DeleteArrayOnExit deleteOnExit(pFrameBuffer);
	RETURN_ON_BAD_HR(hr = pBitmap->CopyPixels(nullptr, stride, bitmapSize, pFrameBuffer));
	std::unique_ptr<TextureManager> pTextureManager = make_unique<TextureManager>();
	pTextureManager->Initialize(pData->RecordingOverlay->DxRes.Context, pData->RecordingOverlay->DxRes.Device);
	ID3D11Texture2D *pTexture;
	RETURN_ON_BAD_HR(pTextureManager->CreateTextureFromBuffer(pFrameBuffer, stride, width, height, &pTexture));
	QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
	pFrame->Frame = pTexture;
	pFrame->Frame->AddRef();
	return hr;
}

HRESULT ReadMedia(CaptureBase &reader, OVERLAY_THREAD_DATA *pData) {
	RECORDING_OVERLAY_DATA *pOverlay = pData->RecordingOverlay;
	HRESULT	hr = reader.Initialize(pOverlay->DxRes.Context, pOverlay->DxRes.Device);
	hr = reader.StartCapture(pOverlay->Source);
	if (FAILED(hr))
	{
		return hr;
	}
	// D3D objects
	ID3D11Texture2D *SharedSurf = nullptr;
	IDXGIKeyedMutex *KeyMutex = nullptr;
	FRAME_BASE *pFrameInfo = pOverlay->FrameInfo;
	// Obtain handle to sync shared Surface
	hr = pOverlay->DxRes.Device->OpenSharedResource(pData->TexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&SharedSurf));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Opening shared texture failed");
		return hr;
	}
	ReleaseOnExit releaseSharedSurf(SharedSurf);
	hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&KeyMutex));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to get keyed mutex interface in spawned thread");
		return hr;
	}
	ReleaseOnExit releaseMutex(KeyMutex);

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
		CComPtr<ID3D11Texture2D> pTexture;
		hr = reader.AcquireNextFrame(10, &pTexture);
		if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
			continue;
		}
		else if (FAILED(hr)) {
			break;
		}
		QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
		{
			MeasureExecutionTime measure(L"ReadMedia wait for sync");
			// Try to acquire keyed mutex in order to access shared surface
			hr = KeyMutex->AcquireSync(0, 1000);
		}

		if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
		{
			// Can't use shared surface right now, try again later
			continue;
		}
		else if (FAILED(hr))
		{
			// Generic unknown failure
			LOG_ERROR(L"Unexpected error acquiring KeyMutex");
			break;
		}

		if (pFrameInfo->Frame == nullptr) {
			D3D11_TEXTURE2D_DESC desc;
			pTexture->GetDesc(&desc);
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;
			//SafeRelease(&pData->FrameInfo->Frame);
			//CComPtr<ID3D11Texture2D> tex;
			pOverlay->DxRes.Device->CreateTexture2D(&desc, nullptr, &pFrameInfo->Frame);
		}
		pOverlay->DxRes.Context->CopyResource(pFrameInfo->Frame, pTexture);
		KeyMutex->ReleaseSync(1);
	}
	return hr;
}
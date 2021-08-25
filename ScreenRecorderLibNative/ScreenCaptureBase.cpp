#include "Cleanup.h"
#include <chrono>

using namespace DirectX;
using namespace std::chrono;
using namespace std;



ScreenCaptureBase::ScreenCaptureBase() :
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
	m_TextureManager(nullptr),
	m_OverlayManager(nullptr),
	m_IsCapturing(false)
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
	HRESULT hr = E_UNEXPECTED;
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_TextureManager = make_unique<TextureManager>();
	m_OverlayManager = make_unique<OverlayManager>();
	RETURN_ON_BAD_HR(hr = m_TextureManager->Initialize(m_DeviceContext, m_Device));
	RETURN_ON_BAD_HR(hr = m_OverlayManager->Initialize(m_DeviceContext, m_Device));
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
	for each (RECORDING_SOURCE_DATA *source in CreatedOutputs)
	{
		for (int i = 0; i < overlays.size(); i++)
			source->Overlays.push_back(overlays[i]);
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

	//m_OverlayThreadCount = (UINT)(overlays.size());
	//m_OverlayThreadHandles = new (std::nothrow) HANDLE[m_OverlayThreadCount]{};
	//m_OverlayThreadData = new (std::nothrow) OVERLAY_THREAD_DATA[m_OverlayThreadCount]{};
	//if (!m_OverlayThreadHandles || !m_OverlayThreadData)
	//{
	//	return E_OUTOFMEMORY;
	//}
	//for (UINT i = 0; i < m_OverlayThreadCount; i++)
	//{
	//	auto overlay = overlays.at(i);
	//	m_OverlayThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
	//	m_OverlayThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
	//	m_OverlayThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
	//	m_OverlayThreadData[i].TexSharedHandle = sharedHandle;
	//	m_OverlayThreadData[i].RecordingOverlay = new RECORDING_OVERLAY_DATA(overlay);
	//	RtlZeroMemory(&m_OverlayThreadData[i].RecordingOverlay->DxRes, sizeof(DX_RESOURCES));
	//	m_OverlayThreadData[i].RecordingOverlay->DxRes.Context = m_DeviceContext;
	//	m_OverlayThreadData[i].RecordingOverlay->DxRes.Device = m_Device;
	//	HRESULT hr = InitializeDx(nullptr, &m_OverlayThreadData[i].RecordingOverlay->DxRes);
	//	if (FAILED(hr))
	//	{
	//		return hr;
	//	}

	//	DWORD ThreadId;
	//	m_OverlayThreadHandles[i] = CreateThread(nullptr, 0, OverlayProc, &m_OverlayThreadData[i], 0, &ThreadId);
	//	if (m_OverlayThreadHandles[i] == nullptr)
	//	{
	//		return E_FAIL;
	//	}
	//}

	m_OverlayManager->StartCapture(overlays, hUnexpectedErrorEvent, hExpectedErrorEvent);
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

		bool haveNewFrameData = (IsUpdatedFramesAvailable() || m_OverlayManager->IsUpdatedFramesAvailable()) && IsInitialFrameWriteComplete();
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

		D3D11_TEXTURE2D_DESC desc;
		m_SharedSurf->GetDesc(&desc);
		desc.MiscFlags = 0;
		RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pDesktopFrame));
		m_DeviceContext->CopyResource(pDesktopFrame, m_SharedSurf);

		int updatedOverlaysCount = 0;
		m_OverlayManager->ProcessOverlays(pDesktopFrame, &updatedOverlaysCount);
		//{
		//	MeasureExecutionTime measure(L"ProcessOverlays");
		//	RETURN_ON_BAD_HR(hr = ProcessOverlays(m_SharedSurf, &updatedOverlaysCount));
		//	measure.SetName(string_format(L"ProcessOverlays for %d sources", updatedOverlaysCount));
		//}

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

	//if (m_OverlayThreadHandles) {
	//	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	//	{
	//		if (m_OverlayThreadHandles[i])
	//		{
	//			CloseHandle(m_OverlayThreadHandles[i]);
	//		}
	//	}
	//	delete[] m_OverlayThreadHandles;
	//	m_OverlayThreadHandles = nullptr;
	//}

	//if (m_OverlayThreadData)
	//{
	//	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	//	{
	//		if (m_OverlayThreadData[i].RecordingOverlay) {
	//			CleanDx(&m_OverlayThreadData[i].RecordingOverlay->DxRes);
	//			//if (m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer) {
	//			//	delete[] m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer;
	//			//	m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer = nullptr;
	//			//}
	//			SafeRelease(&m_OverlayThreadData[i].RecordingOverlay->FrameInfo->Frame);
	//			delete m_OverlayThreadData[i].RecordingOverlay->FrameInfo;
	//			delete m_OverlayThreadData[i].RecordingOverlay;
	//			m_OverlayThreadData[i].RecordingOverlay = nullptr;
	//		}
	//	}
	//	delete[] m_OverlayThreadData;
	//	m_OverlayThreadData = nullptr;
	//}

	//m_OverlayThreadCount = 0;

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
	//if (m_OverlayThreadCount != 0)
	//{
	//	WaitForMultipleObjectsEx(m_OverlayThreadCount, m_OverlayThreadHandles, TRUE, INFINITE, FALSE);
	//}
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

bool ScreenCaptureBase::IsUpdatedFramesAvailable()
{
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
	for each (auto &pair in validOutputs)
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



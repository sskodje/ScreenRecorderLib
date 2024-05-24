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
#include "DynamicWait.h"
#include "Exception.h"

using namespace DirectX;
using namespace std::chrono;
using namespace std;
DWORD WINAPI CaptureThreadProc(_In_ void *Param);
DWORD WINAPI OverlayCaptureThreadProc(_In_ void *Param);
_Ret_maybenull_ CaptureBase *CreateCaptureInstance(_In_ RECORDING_SOURCE_BASE *pSource);
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
	m_IsCapturing(false),
	m_OutputOptions(nullptr),
	m_EncoderOptions(nullptr),
	m_MouseOptions(nullptr),
	m_FrameCopy(nullptr)
{
	// Event to tell spawned threads to quit
	m_TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	InitializeCriticalSection(&m_CriticalSection);
}

ScreenCaptureManager::~ScreenCaptureManager()
{
	if (m_IsCapturing) {
		StopCapture();
	}
	Clean();
	DeleteCriticalSection(&m_CriticalSection);
}

//
// Initialize shaders for drawing to screen
//
HRESULT ScreenCaptureManager::Initialize(
	_In_ ID3D11DeviceContext *pDeviceContext,
	_In_ ID3D11Device *pDevice,
	_In_ std::shared_ptr<OUTPUT_OPTIONS> pOutputOptions,
	_In_ std::shared_ptr<ENCODER_OPTIONS> pEncoderOptions,
	_In_ std::shared_ptr<MOUSE_OPTIONS> pMouseOptions)
{
	HRESULT hr = S_OK;
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_OutputOptions = pOutputOptions;
	m_EncoderOptions = pEncoderOptions;
	m_MouseOptions = pMouseOptions;

	m_TextureManager = make_unique<TextureManager>();
	RETURN_ON_BAD_HR(hr = m_TextureManager->Initialize(m_DeviceContext, m_Device));
	return hr;
}

//
// Start up threads for video capture
//
HRESULT ScreenCaptureManager::StartCapture(_In_ const std::vector<RECORDING_SOURCE *> &sources, _In_ const std::vector<RECORDING_OVERLAY *> &overlays, _In_  HANDLE hErrorEvent)
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	ResetEvent(m_TerminateThreadsEvent);

	HRESULT hr = E_FAIL;
	std::vector<RECORDING_SOURCE_DATA *> CreatedOutputs{};
	RETURN_ON_BAD_HR(hr = CreateSharedSurf(sources, &CreatedOutputs, &m_OutputRect));
	m_CaptureThreadCount = (UINT)(CreatedOutputs.size());
	m_CaptureThreadHandles = new (std::nothrow) HANDLE[m_CaptureThreadCount]{};
	m_CaptureThreadData = new (std::nothrow) CAPTURE_THREAD_DATA[m_CaptureThreadCount]{};
	HANDLE *captureStartEventHandles = new (std::nothrow) HANDLE[m_CaptureThreadCount]{};
	DeleteArrayOnExit deleteCaptureStartHandleArray(captureStartEventHandles);
	if (!m_CaptureThreadHandles || !m_CaptureThreadData || !captureStartEventHandles)
	{
		return E_OUTOFMEMORY;
	}
	for (UINT i = 0; i < m_CaptureThreadCount; i++)
	{
		// Event for when a thread has started
		HANDLE startedEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		if (nullptr == startedEvent) {
			LOG_ERROR(L"CreateEvent failed: last error is %u", GetLastError());
			return E_FAIL;
		}
		captureStartEventHandles[i] = startedEvent;
	}
	HANDLE sharedHandle = GetSharedHandle(m_SharedSurf);
	// Create appropriate # of threads for duplication

	for (UINT i = 0; i < m_CaptureThreadCount; i++)
	{
		RECORDING_SOURCE_DATA *data = CreatedOutputs.at(i);
		m_CaptureThreadData[i].ThreadResult = new CAPTURE_RESULT();
		m_CaptureThreadData[i].ErrorEvent = hErrorEvent;
		m_CaptureThreadData[i].StartedEvent = captureStartEventHandles[i];
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
	HANDLE *overlayCaptureStartEventHandles = new (std::nothrow) HANDLE[m_OverlayThreadCount]{};
	DeleteArrayOnExit deleteOverlayCaptureStartHandleArray(overlayCaptureStartEventHandles);
	if (!m_OverlayThreadHandles || !m_OverlayThreadData || !overlayCaptureStartEventHandles)
	{
		return E_OUTOFMEMORY;
	}
	for (UINT i = 0; i < m_OverlayThreadCount; i++)
	{
		// Event for when a thread has started
		HANDLE startedEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		if (nullptr == startedEvent) {
			LOG_ERROR(L"CreateEvent failed: last error is %u", GetLastError());
			return E_FAIL;
		}
		overlayCaptureStartEventHandles[i] = startedEvent;
	}
	for (UINT i = 0; i < m_OverlayThreadCount; i++)
	{
		auto overlay = overlays.at(i);
		m_OverlayThreadData[i].ThreadResult = new CAPTURE_RESULT();
		m_OverlayThreadData[i].ErrorEvent = hErrorEvent;
		m_OverlayThreadData[i].StartedEvent = overlayCaptureStartEventHandles[i];
		m_OverlayThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_OverlayThreadData[i].CanvasTexSharedHandle = sharedHandle;
		m_OverlayThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_OverlayThreadData[i].RecordingOverlay = new RECORDING_OVERLAY_DATA(overlay);
		RtlZeroMemory(&m_OverlayThreadData[i].RecordingOverlay->DxRes, sizeof(DX_RESOURCES));
		RETURN_ON_BAD_HR(hr = InitializeDx(nullptr, &m_OverlayThreadData[i].RecordingOverlay->DxRes));

		DWORD ThreadId;
		m_OverlayThreadHandles[i] = CreateThread(nullptr, 0, OverlayCaptureThreadProc, &m_OverlayThreadData[i], 0, &ThreadId);
		if (m_OverlayThreadHandles[i] == nullptr)
		{
			return E_FAIL;
		}
	}
	if (m_OverlayThreadCount != 0)
	{
		WaitForMultipleObjectsEx(m_OverlayThreadCount, overlayCaptureStartEventHandles, TRUE, INFINITE, FALSE);
		for (UINT i = 0; i < m_OverlayThreadCount; ++i)
		{
			if (overlayCaptureStartEventHandles[i])
			{
				CloseHandle(overlayCaptureStartEventHandles[i]);
			}
		}
	}
	if (m_CaptureThreadCount != 0) {
		WaitForMultipleObjectsEx(m_CaptureThreadCount, captureStartEventHandles, TRUE, INFINITE, FALSE);
		for (UINT i = 0; i < m_CaptureThreadCount; ++i)
		{
			if (captureStartEventHandles[i])
			{
				CloseHandle(captureStartEventHandles[i]);
			}
		}
	}

	m_IsCapturing = true;
	return hr;
}

HRESULT ScreenCaptureManager::StopCapture()
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	LOG_TRACE("Stopping capture threads");
	if (!SetEvent(m_TerminateThreadsEvent)) {
		LOG_ERROR("Could not terminate capture threads");
		return E_FAIL;
	}
	HRESULT hr = WaitForThreadTermination();
	m_IsCapturing = false;
	return hr;
}

HRESULT ScreenCaptureManager::CopyCurrentFrame(_Out_ CAPTURED_FRAME *pFrame)
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
	D3D11_TEXTURE2D_DESC desc;
	m_FrameCopy->GetDesc(&desc);
	ID3D11Texture2D *pFrameCopy;
	RETURN_ON_BAD_HR(m_Device->CreateTexture2D(&desc, nullptr, &pFrameCopy));

	m_DeviceContext->CopyResource(pFrameCopy, m_FrameCopy);
	RtlZeroMemory(pFrame, sizeof(pFrame));
	pFrame->Frame = pFrameCopy;
	pFrame->PtrInfo = m_PtrInfo;
	pFrame->FrameUpdateCount = 0;
	return S_OK;
}

HRESULT ScreenCaptureManager::AcquireNextFrame(_In_  double timeUntilNextFrame, _In_ double maxFrameLength, _Out_ CAPTURED_FRAME *pFrame)
{
	HRESULT hr;
	auto  start = std::chrono::steady_clock::now();
	bool haveNewFrame = false;
	auto GetMillisUntilNextFrame([&]()
		{
			auto millisWaited = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (m_EncoderOptions->GetIsFixedFramerate() || haveNewFrame) {
				return timeUntilNextFrame - millisWaited;
			}
			else {
				return maxFrameLength - millisWaited;
			}
		});
	auto GetNextSyncTimeout([&]()
		{
			return static_cast<DWORD>(max(haveNewFrame ? 0 : 1, floor(GetMillisUntilNextFrame()) - 0.5));
		});
	auto ShouldDelay([&]()
		{
			auto millisWaited = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (!IsInitialFrameWriteComplete() && millisWaited < maxFrameLength) {
				return true;
			}
			if (m_LastAcquiredFrameTimeStamp.QuadPart == 0 && IsInitialFrameWriteComplete()) {
				return false;
			}
			if (m_OutputOptions->GetRecorderMode() == RecorderModeInternal::Video) {
				if (!m_EncoderOptions->GetIsFixedFramerate()
					&& ((m_MouseOptions->IsMousePointerEnabled() && m_PtrInfo.IsPointerShapeUpdated)//and never delay when pointer changes if we draw pointer
						|| false)) // Or if we need to write a snapshot 
				{
					return false;
				}
			}
			auto millisUntilNextFrame = GetMillisUntilNextFrame();
			if (millisUntilNextFrame < 0.1) {
				return false;
			}
			else if (millisUntilNextFrame >= 0.1) {
				return true;
			}
			return false;
		});

	DWORD syncTimeout = GetNextSyncTimeout();

	while (true)
	{
		// Try to acquire keyed mutex in order to access shared surface
		hr = m_KeyMutex->AcquireSync(1, syncTimeout);

		if (hr == static_cast<HRESULT>(WAIT_TIMEOUT)) {
			if (!ShouldDelay()) {
				hr = m_KeyMutex->AcquireSync(0, 0);
				if (SUCCEEDED(hr)) {
					hr = m_KeyMutex->ReleaseSync(1);
					syncTimeout = 0;
				}
			}
			else {
				syncTimeout = GetNextSyncTimeout();
			}
		}
		else if (hr == static_cast<HRESULT>(WAIT_ABANDONED)) {
			return E_FAIL;
		}
		else if (SUCCEEDED(hr)) {
			haveNewFrame = true;
			if (ShouldDelay()) {
				m_KeyMutex->ReleaseSync(0);
				syncTimeout = GetNextSyncTimeout();
			}
			else {
				break;
			}
		}
		else if (FAILED(hr)) {
			return hr;
		}
	}
	{
		ReleaseKeyedMutexOnExit releaseMutex(m_KeyMutex, 0);
		MeasureExecutionTime measure(L"AcquireNextFrame lock");
		int updatedFrameCount = GetUpdatedSourceCount();
		int updatedOverlaysCount = GetUpdatedOverlayCount();

		if (!m_FrameCopy) {
			D3D11_TEXTURE2D_DESC desc;
			m_SharedSurf->GetDesc(&desc);
			desc.MiscFlags = 0;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &m_FrameCopy));
		}
		if (m_OutputOptions->IsVideoCaptureEnabled()) {
			m_DeviceContext->CopyResource(m_FrameCopy, m_SharedSurf);
		}
		if (updatedFrameCount > 0 || updatedOverlaysCount > 0) {
			QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);
		}
		m_PtrInfo.IsPointerShapeUpdated = false;
		RtlZeroMemory(pFrame, sizeof(pFrame));
		pFrame->Frame = m_FrameCopy;
		pFrame->PtrInfo = m_PtrInfo;
		pFrame->FrameUpdateCount = updatedFrameCount;
	}
	return hr;
}

//
// Clean up resources
//
void ScreenCaptureManager::Clean()
{
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveOnExit(&m_CriticalSection);
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
				delete m_CaptureThreadData[i].ThreadResult;
				m_CaptureThreadData[i].ThreadResult = nullptr;
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
				delete m_OverlayThreadData[i].ThreadResult;
				m_OverlayThreadData[i].ThreadResult = nullptr;
			}
		}
		delete[] m_OverlayThreadData;
		m_OverlayThreadData = nullptr;
	}

	m_OverlayThreadCount = 0;

	CloseHandle(m_TerminateThreadsEvent);
}

//
// Waits for all spawned threads to terminate
//
HRESULT ScreenCaptureManager::WaitForThreadTermination()
{
	LOG_TRACE("Waiting for capture thread termination..");
	if (m_OverlayThreadCount != 0) {
		if (WaitForMultipleObjects(m_OverlayThreadCount, m_OverlayThreadHandles, TRUE, 5000) == WAIT_TIMEOUT) {
			LOG_ERROR(L"Timeout in overlay capture thread termination");
			return E_FAIL;
		}
	}
	if (m_CaptureThreadCount != 0) {
		if (WaitForMultipleObjects(m_CaptureThreadCount, m_CaptureThreadHandles, TRUE, 5000) == WAIT_TIMEOUT) {
			LOG_ERROR(L"Timeout in capture thread termination");
			return E_FAIL;
		}
	}
	return S_OK;
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
			if (!FAILED(m_OverlayThreadData[i].ThreadResult->RecordingResult) && m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart == 0) {
				//If any of the overlays have not yet written a frame, we return and wait for them.
				return false;
			}
		}
	}
	return true;
}

UINT ScreenCaptureManager::GetUpdatedSourceCount()
{
	int updatedFrameCount = 0;

	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		if (m_CaptureThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			updatedFrameCount++;
		}
	}
	return updatedFrameCount;
}

UINT ScreenCaptureManager::GetUpdatedOverlayCount()
{
	int updatedFrameCount = 0;

	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			updatedFrameCount++;
		}
	}
	return updatedFrameCount;
}

std::vector<CAPTURE_RESULT *> ScreenCaptureManager::GetCaptureResults()
{
	std::vector<CAPTURE_RESULT *> results;
	for each (CAPTURE_THREAD_DATA threadData in GetCaptureThreadData())
	{
		results.push_back(threadData.ThreadResult);
	}
	for each (OVERLAY_THREAD_DATA threadData in GetOverlayThreadData())
	{
		results.push_back(threadData.ThreadResult);
	}
	return results;
}

std::vector<CAPTURE_THREAD_DATA> ScreenCaptureManager::GetCaptureThreadData()
{
	std::vector<CAPTURE_THREAD_DATA> threadData;
	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		threadData.push_back(m_CaptureThreadData[i]);
	}
	return threadData;
}

std::vector<OVERLAY_THREAD_DATA> ScreenCaptureManager::GetOverlayThreadData()
{
	std::vector<OVERLAY_THREAD_DATA> threadData;
	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		threadData.push_back(m_OverlayThreadData[i]);
	}
	return threadData;
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
			if (FAILED(m_OverlayThreadData[i].ThreadResult->RecordingResult) && !m_OverlayThreadData[i].ThreadResult->IsRecoverableError) {
				continue;
			}
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
	_set_se_translator(ExceptionTranslator);
	// Data passed in from thread creation
	CAPTURE_THREAD_DATA *pData = static_cast<CAPTURE_THREAD_DATA *>(Param);
	RECORDING_SOURCE_DATA *pSourceData = pData->RecordingSource;
	RECORDING_SOURCE *pSource = pSourceData->RecordingSource;

	DynamicWait retryWait;
	retryWait.SetWaitBands({
					  {25, 5},
					  {250, 5},
					  {500, WAIT_BAND_STOP}
					});
	int retryCount = 0;
	bool isCapturingVideo = true;
	bool isSharedSurfaceDirty = false;
	bool waitToProcessCurrentFrame = false;

	hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(hr)) {
		goto Exit;
	}

	//This scope must be here for ReleaseOnExit to work.
Start:
	{
		try
		{
			std::unique_ptr<CaptureBase> pRecordingSourceCapture = nullptr;
			// D3D objects
			CComPtr<ID3D11Texture2D> SharedSurf = nullptr;
			CComPtr<IDXGIKeyedMutex> KeyMutex = nullptr;
			SetEvent(pData->StartedEvent);

			if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
				hr = S_OK;
				goto Exit;
			}

			pRecordingSourceCapture.reset(CreateCaptureInstance(pSource));
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
				LOG_ERROR(L"Failed to initialize recording source %ls", pRecordingSourceCapture->Name().c_str());
				goto Exit;
			}
			hr = pRecordingSourceCapture->StartCapture(*pSource);

			if (FAILED(hr))
			{
				LOG_ERROR(L"Failed to start capture");
				goto Exit;
			}
			TextureManager textureManager{};
			hr = textureManager.Initialize(pSourceData->DxRes.Context, pSourceData->DxRes.Device);
			if (FAILED(hr))
			{
				LOG_ERROR(L"Failed to initialize TextureManager");
				goto Exit;
			}
			ExecuteFuncOnExit blankFrameOnExit([&]() {
				if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) != WAIT_OBJECT_0) {
					if (KeyMutex->AcquireSync(0, 500) == S_OK) {
						textureManager.BlankTexture(SharedSurf, pSourceData->FrameCoordinates, pSourceData->OffsetX, pSourceData->OffsetY);
						KeyMutex->ReleaseSync(1);
					}
				}
			});

			const IStream *sourceStream = pSource->SourceStream;
			const std::wstring sourcePath = pSource->SourcePath;
			const HWND sourceWindowHandle = pSource->SourceWindow;
			auto IsSourceChanged([sourceStream, sourcePath, sourceWindowHandle](RECORDING_SOURCE *source) {
				return source->SourcePath != sourcePath || source->SourceStream != sourceStream || source->SourceWindow != sourceWindowHandle;
			});

			*pData->ThreadResult = {};
			pData->ThreadResult->RecordingResult = S_OK;
			// Main duplication loop
			std::chrono::steady_clock::time_point WaitForFrameBegin = (std::chrono::steady_clock::time_point::min)();
			while (true)
			{
				if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
					hr = S_OK;
					break;
				}

				if (IsSourceChanged(pSource)) {
					goto Start;
				}

				if (!isCapturingVideo) {
					Sleep(1);
					if (pSource->IsVideoCaptureEnabled.value_or(true)) {
						isCapturingVideo = true;
						isSharedSurfaceDirty = true;
					}
					continue;
				}
				CComPtr<ID3D11Texture2D> pFrame = nullptr;
				if (!waitToProcessCurrentFrame)
				{
					if (isSharedSurfaceDirty) {
						hr = pRecordingSourceCapture->AcquireNextFrame(10, &pFrame);
					}
					else {
						hr = pRecordingSourceCapture->AcquireNextFrame(10, nullptr);
					}
					if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
						continue;
					}
					else if (hr == S_FALSE) {
						Sleep(10);
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
					// Can't use shared surface right now, try again later
					if (!waitToProcessCurrentFrame) {
						WaitForFrameBegin = chrono::steady_clock::now();
					}
					waitToProcessCurrentFrame = true;
					continue;
				}
				else if (FAILED(hr))
				{
					// Generic unknown failure
					LOG_ERROR(L"Unexpected error acquiring KeyMutex");
					break;
				}
#if MEASURE_EXECUTION_TIME
				MeasureExecutionTime measureLock(string_format(L"CaptureThreadProc sync lock for %ls", pRecordingSourceCapture->Name().c_str()));
#endif
				ReleaseKeyedMutexOnExit releaseMutex(KeyMutex, 1);

				// We can now process the current frame
				if (waitToProcessCurrentFrame) {
					waitToProcessCurrentFrame = false;
					LONGLONG waitTimeMillis = duration_cast<milliseconds>(chrono::steady_clock::now() - WaitForFrameBegin).count();
					//If the capture has been waiting for an excessive time to draw a frame, we assume the frame is stale, and drop it.
					if (pData->TotalUpdatedFrameCount > 0 && waitTimeMillis > 1000) {
						isSharedSurfaceDirty = true;
						LOG_DEBUG("Dropped %ls frame because wait time exceeded limit", pRecordingSourceCapture->Name().c_str());
						continue;
					}
					LOG_TRACE(L"CaptureThreadProc waited for busy shared surface for %lld ms", waitTimeMillis);
				}
				if (pSource->IsCursorCaptureEnabled.value_or(true)) {
					// Get mouse info
					hr = pRecordingSourceCapture->GetMouse(pData->PtrInfo, pSourceData->FrameCoordinates, pSourceData->OffsetX, pSourceData->OffsetY);
					if (FAILED(hr)) {
						LOG_ERROR("Failed to get mouse data");
					}
				}
				else if (pData->PtrInfo) {
					pData->PtrInfo->Visible = false;
				}

				if (pSource->IsVideoCaptureEnabled.value_or(true)) {
					if (isSharedSurfaceDirty && pFrame) {
						//The screen has been blacked out, so we restore a full frame to the shared surface before starting to apply updates.
						RECT offsetFrameCoordinates = pSourceData->FrameCoordinates;
						OffsetRect(&offsetFrameCoordinates, pSourceData->OffsetX, pSourceData->OffsetY);
						hr = textureManager.DrawTexture(SharedSurf, pFrame, offsetFrameCoordinates);
						isSharedSurfaceDirty = false;
					}
					hr = pRecordingSourceCapture->WriteNextFrameToSharedSurface(0, SharedSurf, pSourceData->OffsetX, pSourceData->OffsetY, pSourceData->FrameCoordinates);
				}
				else {
					hr = textureManager.BlankTexture(SharedSurf, pSourceData->FrameCoordinates, pSourceData->OffsetX, pSourceData->OffsetY);
					if (SUCCEEDED(hr)) {
						isCapturingVideo = false;
					}
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
				pData->TotalUpdatedFrameCount++;
				QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
			}
		}
		catch (const AccessViolationException &ex) {
			hr = EXCEPTION_ACCESS_VIOLATION;
		}
		catch (...) {
			hr = E_UNEXPECTED;
		}
	}
Exit:
	if (pData->ThreadResult) {
		//E_ABORT is returned when the capture loop should be stopped, but the recording continue. On other errors, we check how to handle them.
		if (hr == E_ABORT) {
			hr = S_OK;
		}
		pData->ThreadResult->RecordingResult = hr;
		if (FAILED(hr))
		{
			ProcessCaptureHRESULT(hr, pData->ThreadResult, pSourceData->DxRes.Device);
			if (pData->ThreadResult->IsRecoverableError) {
				if (pData->ThreadResult->IsDeviceError) {
					LOG_INFO("Recoverable device error in screen capture, reinitializing devices and capture..");
					SetEvent(pData->ErrorEvent);
				}
				else {
					LOG_INFO("Recoverable error in screen capture, reinitializing..");
					isSharedSurfaceDirty = true;
					if (pData->ThreadResult->NumberOfRetries == INFINITE
						|| retryCount <= pData->ThreadResult->NumberOfRetries) {
						retryCount++;
						retryWait.Wait();
						goto Start;
					}
					else {
						pData->ThreadResult->IsRecoverableError = false;
						SetEvent(pData->ErrorEvent);
						LOG_ERROR("Retry count of %d exceeded in screen capture, exiting..", pData->ThreadResult->NumberOfRetries);
					}
				}
			}
			else {
				SetEvent(pData->ErrorEvent);
				LOG_ERROR("Fatal error in screen capture, exiting..");
			}
		}
	}
	CoUninitialize();
	LOG_DEBUG("Exiting CaptureThreadProc");
	return 0;
}


DWORD WINAPI OverlayCaptureThreadProc(_In_ void *Param) {
	HRESULT hr = E_FAIL;
	_set_se_translator(ExceptionTranslator);
	// Data passed in from thread creation
	OVERLAY_THREAD_DATA *pData = static_cast<OVERLAY_THREAD_DATA *>(Param);

	RECORDING_OVERLAY_DATA *pOverlayData = pData->RecordingOverlay;
	RECORDING_OVERLAY *pOverlay = pOverlayData->RecordingOverlay;

	DynamicWait retryWait;
	retryWait.SetWaitBands({
						  {25, 5},
						  {250, 5},
						  {500, WAIT_BAND_STOP}
						});
	int retryCount = 0;
	bool IsCapturingVideo = true;

	hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(hr)) {
		goto Exit;
	}

Start:
	{
		try
		{
			unique_ptr<CaptureBase> overlayCapture = nullptr;
			// D3D objects
			CComPtr<ID3D11Texture2D> pSharedTexture = nullptr;
			CComPtr<ID3D11Texture2D> pCurrentFrame = nullptr;
			CComPtr<ID3D11Texture2D> SharedSurf = nullptr;
			CComPtr<IDXGIKeyedMutex> KeyMutex = nullptr;

			SetEvent(pData->StartedEvent);

			if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
				hr = S_OK;
				goto Exit;
			}

			overlayCapture.reset(CreateCaptureInstance(pOverlay));
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

			ExecuteFuncOnExit blankFrameOnExit([&]() {
				if (pSharedTexture) {
					D3D11_TEXTURE2D_DESC desc;
					pSharedTexture->GetDesc(&desc);
					CComPtr<ID3D11Texture2D> pBlankTexture;
					pOverlayData->DxRes.Device->CreateTexture2D(&desc, nullptr, &pBlankTexture);
					pOverlayData->DxRes.Context->CopyResource(pSharedTexture, pBlankTexture);
				}
			});
			const IStream *sourceStream = pOverlay->SourceStream;
			const std::wstring sourcePath = pOverlay->SourcePath;
			const HWND sourceWindowHandle = pOverlay->SourceWindow;

			auto IsSourceChanged([sourceStream, sourcePath, sourceWindowHandle](RECORDING_OVERLAY *overlay) {
				return overlay->SourcePath != sourcePath || overlay->SourceStream != sourceStream || overlay->SourceWindow != sourceWindowHandle;
				});

			*pData->ThreadResult = {};
			pData->ThreadResult->RecordingResult = S_OK;
			// Main capture loop
			while (true)
			{
				if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
					hr = S_OK;
					break;
				}

				if (IsSourceChanged(pOverlay)) {
					goto Start;
				}

				if (!IsCapturingVideo) {
					Sleep(1);
					IsCapturingVideo = pOverlay->IsVideoCaptureEnabled.value_or(true);
					continue;
				}
				pCurrentFrame.Release();
				// Get new frame from video capture
				hr = overlayCapture->AcquireNextFrame(10, &pCurrentFrame);
				if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
					continue;
				}
				else if (hr == S_FALSE) {
					Sleep(10);
					continue;
				}
				else if (FAILED(hr)) {
					break;
				}

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

				if (!pOverlay->IsVideoCaptureEnabled.value_or(true)) {
					D3D11_TEXTURE2D_DESC desc;
					pCurrentFrame->GetDesc(&desc);
					pCurrentFrame.Release();
					pOverlayData->DxRes.Device->CreateTexture2D(&desc, nullptr, &pCurrentFrame);
					IsCapturingVideo = false;
				}

				pOverlayData->DxRes.Context->CopyResource(pSharedTexture, pCurrentFrame);
				//If a shared texture is updated on one device ID3D11DeviceContext::Flush must be called on that device. 
				//https://docs.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-opensharedresource
				pOverlayData->DxRes.Context->Flush();
				QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
				// Try to acquire keyed mutex in order to access shared surface. The timeout value is 0, and we just continue if we don't get a lock.
				// This is just used to notify the rendering loop about updated overlays, so no reason to wait around if it's already updating.
				if (KeyMutex->AcquireSync(0, 0) == S_OK) {
					KeyMutex->ReleaseSync(1);
				}
			}
		}
		catch (const AccessViolationException &e) {
			hr = EXCEPTION_ACCESS_VIOLATION;
			LOG_ERROR(L"Exception in WASAPICapture: AccessViolationException");
		}
		catch (...) {
			hr = E_UNEXPECTED;
			LOG_ERROR(L"Exception in WASAPICapture");
		}
	}
Exit:
	if (pData->ThreadResult) {
		//E_ABORT is returned when the capture loop should be stopped, but the recording continue. On other errors, we check how to handle them.
		if (hr == E_ABORT) {
			hr = S_OK;
		}
		pData->ThreadResult->RecordingResult = hr;
		if (FAILED(hr))
		{
			ProcessCaptureHRESULT(hr, pData->ThreadResult, pOverlayData->DxRes.Device);
			if (pData->ThreadResult->IsRecoverableError) {
				if (pData->ThreadResult->IsDeviceError) {
					LOG_INFO("Recoverable device error in overlay capture, reinitializing devices and capture..");
					SetEvent(pData->ErrorEvent);
				}
				else {
					LOG_INFO("Recoverable error in overlay capture, reinitializing..");
					if (pData->ThreadResult->NumberOfRetries == INFINITE
						|| retryCount <= pData->ThreadResult->NumberOfRetries) {
						retryCount++;
						retryWait.Wait();
						goto Start;
					}
					else {
						pData->ThreadResult->IsRecoverableError = false;
						SetEvent(pData->ErrorEvent);
						LOG_ERROR("Retry count of %d exceeded in overlay capture, exiting..", pData->ThreadResult->NumberOfRetries);
					}
				}
			}
			else {
				SetEvent(pData->ErrorEvent);
				LOG_ERROR("Fatal error in overlay capture, exiting..");
			}
		}
	}
	CoUninitialize();
	LOG_DEBUG("Exiting OverlayCaptureThreadProc");
	return 0;
}

_Ret_maybenull_ CaptureBase *CreateCaptureInstance(_In_ RECORDING_SOURCE_BASE *pSource)
{
	switch (pSource->Type)
	{
		case RecordingSourceType::CameraCapture: {
			return new CameraCapture();
		}
		case RecordingSourceType::Display: {
			RECORDING_SOURCE *pRecordingSource = dynamic_cast<RECORDING_SOURCE *>(pSource);
			if (pRecordingSource) {
				if (pRecordingSource->SourceApi == RecordingSourceApi::DesktopDuplication) {
					return new DesktopDuplicationCapture();
				}
				else if (pRecordingSource->SourceApi == RecordingSourceApi::WindowsGraphicsCapture) {
					return new WindowsGraphicsCapture();
				}
				else {
					return nullptr;
				}
			}
			else {
				return new WindowsGraphicsCapture();
			}
		}
		case RecordingSourceType::Picture: {
			std::string signature = "";

			if (pSource->SourceStream) {
				signature = ReadFileSignature(pSource->SourceStream);
			}
			else {
				signature = ReadFileSignature(pSource->SourcePath.c_str());
			}
			ImageFileType imageType = getImageTypeByMagic(signature.c_str());
			if (imageType == ImageFileType::IMAGE_FILE_GIF) {
				return new GifReader();
			}
			else {
				return new ImageReader();
			}
		}
		case RecordingSourceType::Video: {
			return new VideoReader();
		}
		case RecordingSourceType::Window: {
			return new WindowsGraphicsCapture();
		}
		default:
			return nullptr;
	}
}

void ProcessCaptureHRESULT(_In_ HRESULT hr, _Inout_ CAPTURE_RESULT *pResult, _In_opt_ ID3D11Device *pDevice) {
	*pResult = {};
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
					case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
					case static_cast<HRESULT>(E_OUTOFMEMORY):
					{
						LOG_ERROR(L"Graphics device unavailable: hr = 0x%08x, error = %s", hr, err.ErrorMessage());
						pResult->IsRecoverableError = true;
						pResult->IsDeviceError = true;
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
			LOG_ERROR(L"Desktop temporarily unavailable: hr = 0x%08x, error = %s", hr, err.ErrorMessage());
			break;
		case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
			pResult->Error = L"Desktop Duplication for output is currently unavailable.";
			LOG_ERROR(L"Error in capture with DXGI_ERROR_NOT_CURRENTLY_AVAILABLE. This probably means DXGI reached the limit on the maximum number of concurrent duplication applications (default of four). Therefore, the calling application cannot create any desktop duplication interfaces until the other applications close");
			break;
		case DXGI_ERROR_NOT_FOUND:
			pResult->IsRecoverableError = true;
			pResult->Error = L"Device not found";
			pResult->NumberOfRetries = 30;
			break;
		case EXCEPTION_ACCESS_VIOLATION:
			pResult->IsRecoverableError = true;
			pResult->IsDeviceError = true;
			pResult->Error = L"Access Violation Exception";
			pResult->NumberOfRetries = 5;
			break;
		default:
			//Unexpected error, return.
			LOG_ERROR(L"Unexpected error, aborting capture: hr = 0x%08x, error = %s", hr, err.ErrorMessage());
			pResult->Error = L"Unexpected error, aborting capture";
			break;
	}
}
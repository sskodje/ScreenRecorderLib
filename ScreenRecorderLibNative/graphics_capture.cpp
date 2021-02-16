#include "graphics_capture.h"
#include "graphics_capture.util.h"
#include <atlbase.h>
#include "utilities.h"
#include "graphics_manager.h"
#include "monitor_list.h"
#include "cleanup.h"
namespace winrt
{
	using namespace Windows::Foundation;
	using namespace Windows::System;
	using namespace Windows::Graphics;
	using namespace Windows::Graphics::Capture;
	using namespace Windows::Graphics::DirectX;
	using namespace Windows::Graphics::DirectX::Direct3D11;
	using namespace Windows::Foundation::Numerics;
	using namespace Windows::UI;
	using namespace Windows::UI::Composition;
}

using namespace capture::util;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
static DWORD WINAPI GCProc(_In_ void* Param);

graphics_capture::graphics_capture(_In_ ID3D11Device* pDevice, _In_ ID3D11DeviceContext *pDeviceContext, _In_ bool isCursorCaptureEnabled) :
	m_Device(nullptr),
	m_ImmediateContext(nullptr),
	m_TerminateThreadsEvent(nullptr),
	m_LastAcquiredFrameTimeStamp{},
	m_OutputRect{},
	m_SharedSurf(nullptr),
	m_KeyMutex(nullptr),
	m_ThreadCount(0),
	m_ThreadHandles(nullptr),
	m_ThreadData(nullptr)
{
	m_isCursorCaptureEnabled = isCursorCaptureEnabled;
	m_Device = pDevice;
	m_ImmediateContext = pDeviceContext;

	// Event to tell spawned threads to quit
	m_TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

graphics_capture::~graphics_capture()
{
	Clean();
}

//
// Clean up resources
//
void graphics_capture::Clean()
{
	if (m_SharedSurf) {
		m_SharedSurf->Release();
		m_SharedSurf = nullptr;
	}
	if (m_KeyMutex){
		m_KeyMutex->Release();
		m_KeyMutex = nullptr;
	}

	if (m_ThreadHandles){
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

	if (m_ThreadData){
		for (UINT i = 0; i < m_ThreadCount; ++i)
		{
			CleanDx(&m_ThreadData[i].DxRes);
		}
		delete[] m_ThreadData;
		m_ThreadData = nullptr;
	}

	m_ThreadCount = 0;
	CloseHandle(m_TerminateThreadsEvent);
}

HRESULT graphics_capture::StartCapture(std::vector<std::wstring> const &outputs, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent)
{
	ResetEvent(m_TerminateThreadsEvent);
	std::vector<std::wstring> CreatedOutputs;
	HRESULT hr = CreateSharedSurf(outputs, &CreatedOutputs, &m_OutputRect);
	if (FAILED(hr)) {
		return hr;
	}
	m_ThreadCount = CreatedOutputs.size();
	m_ThreadHandles = new (std::nothrow) HANDLE[m_ThreadCount]{};
	m_ThreadData = new (std::nothrow) THREAD_DATA[m_ThreadCount]{};
	if (!m_ThreadHandles || !m_ThreadData)
	{
		return E_OUTOFMEMORY;
	}
	HANDLE sharedHandle = GetSharedHandle();

	// Create appropriate # of threads for capture
	for (UINT i = 0; i < m_ThreadCount; i++)
	{
		std::wstring outputName = CreatedOutputs.at(i);
		m_ThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_ThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_ThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_ThreadData[i].OutputMonitor = outputName;
		m_ThreadData[i].TexSharedHandle = sharedHandle;
		m_ThreadData[i].OffsetX = m_OutputRect.left;
		m_ThreadData[i].OffsetY = m_OutputRect.top;
		m_ThreadData[i].PtrInfo = nullptr;
		m_ThreadData[i].IsCursorCaptureEnabled = m_isCursorCaptureEnabled;
		RtlZeroMemory(&m_ThreadData[i].DxRes, sizeof(DX_RESOURCES));
		HRESULT hr = InitializeDx(&m_ThreadData[i].DxRes);
		if (FAILED(hr))
		{
			return hr;
		}

		DWORD ThreadId;
		m_ThreadHandles[i] = CreateThread(nullptr, 0, GCProc, &m_ThreadData[i], 0, &ThreadId);
		if (m_ThreadHandles[i] == nullptr)
		{
			return E_FAIL;
		}
	}
	return hr;
}

HRESULT graphics_capture::StartCapture(HWND windowhandle, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent)
{
	ResetEvent(m_TerminateThreadsEvent);

	HRESULT hr = CreateSharedSurf(windowhandle, &m_OutputRect);
	if (FAILED(hr)) {
		return hr;
	}
	m_ThreadCount = 1;
	m_ThreadHandles = new (std::nothrow) HANDLE[m_ThreadCount]{};
	m_ThreadData = new (std::nothrow) THREAD_DATA[m_ThreadCount]{};
	if (!m_ThreadHandles || !m_ThreadData)
	{
		return E_OUTOFMEMORY;
	}
	HANDLE sharedHandle = GetSharedHandle();

	// Create appropriate # of threads for capture
	for (UINT i = 0; i < m_ThreadCount; i++)
	{
		m_ThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_ThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_ThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_ThreadData[i].OutputWindow = windowhandle;
		m_ThreadData[i].TexSharedHandle = sharedHandle;
		m_ThreadData[i].OffsetX = m_OutputRect.left;
		m_ThreadData[i].OffsetY = m_OutputRect.top;
		m_ThreadData[i].PtrInfo = nullptr;
		m_ThreadData[i].IsCursorCaptureEnabled = m_isCursorCaptureEnabled;
		RtlZeroMemory(&m_ThreadData[i].DxRes, sizeof(DX_RESOURCES));
		HRESULT hr = InitializeDx(&m_ThreadData[i].DxRes);
		if (FAILED(hr))
		{
			return hr;
		}

		DWORD ThreadId;
		m_ThreadHandles[i] = CreateThread(nullptr, 0, GCProc, &m_ThreadData[i], 0, &ThreadId);
		if (m_ThreadHandles[i] == nullptr)
		{
			return E_FAIL;
		}
	}
	return hr;
}


//
// Returns shared handle
//
HANDLE graphics_capture::GetSharedHandle()
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

//
// Create shared texture
//
HRESULT graphics_capture::CreateSharedSurf(_In_ HWND windowhandle, _Out_ RECT* pDeskBounds)
{
	HRESULT	hr;

	IDXGIOutput* DxgiOutput = nullptr;

	// Figure out right dimensions for full size desktop texture and # of outputs to capture
	auto captureItem = capture::util::CreateCaptureItemForWindow(windowhandle);
	pDeskBounds->left = 0;
	pDeskBounds->top = 0;
	pDeskBounds->right = captureItem.Size().Width;
	pDeskBounds->bottom = captureItem.Size().Height;

	// Create shared texture for all capture threads to draw into
	D3D11_TEXTURE2D_DESC DeskTexD;
	RtlZeroMemory(&DeskTexD, sizeof(D3D11_TEXTURE2D_DESC));
	DeskTexD.Width = pDeskBounds->right - pDeskBounds->left;
	DeskTexD.Height = pDeskBounds->bottom - pDeskBounds->top;
	DeskTexD.MipLevels = 1;
	DeskTexD.ArraySize = 1;
	DeskTexD.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	DeskTexD.SampleDesc.Count = 1;
	DeskTexD.Usage = D3D11_USAGE_DEFAULT;
	DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	DeskTexD.CPUAccessFlags = 0;
	DeskTexD.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

	hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_SharedSurf);
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

//
// Create shared texture
//
HRESULT graphics_capture::CreateSharedSurf(_In_ std::vector<std::wstring> outputs, _Out_ std::vector<std::wstring> *pCreatedOutputs, _Out_ RECT* pDeskBounds)
{
	// Get DXGI resources
	IDXGIDevice* DxgiDevice = nullptr;
	HRESULT hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to QI for DXGI Device");
		return hr;
	}

	IDXGIAdapter* DxgiAdapter = nullptr;
	hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
	DxgiDevice->Release();
	DxgiDevice = nullptr;
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to get parent DXGI Adapter");
		return hr;
	}

	// Set initial values so that we always catch the right coordinates
	pDeskBounds->left = INT_MAX;
	pDeskBounds->right = INT_MIN;
	pDeskBounds->top = INT_MAX;
	pDeskBounds->bottom = INT_MIN;

	IDXGIOutput* DxgiOutput = nullptr;

	// Figure out right dimensions for full size desktop texture and # of outputs to capture
	std::vector<std::wstring> createdOutputs{};

	hr = S_OK;
	for (int i = 0; SUCCEEDED(hr); i++)
	{
		if (DxgiOutput)
		{
			DxgiOutput->Release();
			DxgiOutput = nullptr;
		}
		hr = DxgiAdapter->EnumOutputs(i, &DxgiOutput);
		if (DxgiOutput && (hr != DXGI_ERROR_NOT_FOUND))
		{
			DXGI_OUTPUT_DESC DesktopDesc;
			DxgiOutput->GetDesc(&DesktopDesc);
			if (outputs.size() == 0 || std::find(outputs.begin(), outputs.end(), DesktopDesc.DeviceName) != outputs.end())
			{
				pDeskBounds->left = min(DesktopDesc.DesktopCoordinates.left, pDeskBounds->left);
				pDeskBounds->top = min(DesktopDesc.DesktopCoordinates.top, pDeskBounds->top);
				pDeskBounds->right = max(DesktopDesc.DesktopCoordinates.right, pDeskBounds->right);
				pDeskBounds->bottom = max(DesktopDesc.DesktopCoordinates.bottom, pDeskBounds->bottom);
				createdOutputs.push_back(DesktopDesc.DeviceName);
			}
		}
	}

	DxgiAdapter->Release();
	DxgiAdapter = nullptr;

	// Set created outputs
	*pCreatedOutputs = createdOutputs;

	if (createdOutputs.size() == 0)
	{
		// We could not find any outputs
		return E_FAIL;
	}

	// Create shared texture for all capture threads to draw into
	D3D11_TEXTURE2D_DESC DeskTexD;
	RtlZeroMemory(&DeskTexD, sizeof(D3D11_TEXTURE2D_DESC));
	DeskTexD.Width = pDeskBounds->right - pDeskBounds->left;
	DeskTexD.Height = pDeskBounds->bottom - pDeskBounds->top;
	DeskTexD.MipLevels = 1;
	DeskTexD.ArraySize = 1;
	DeskTexD.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	DeskTexD.SampleDesc.Count = 1;
	DeskTexD.Usage = D3D11_USAGE_DEFAULT;
	DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	DeskTexD.CPUAccessFlags = 0;
	DeskTexD.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

	hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_SharedSurf);
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



HRESULT graphics_capture::AcquireNextFrame(_In_  DWORD timeoutMillis, _Out_ CAPTURED_FRAME *frame)
{
	bool haveNewFrameData = false;
	int updatedFrameCount = 0;
	SIZE contentSize{};
	for (UINT i = 0; i < m_ThreadCount; ++i)
	{
		if (m_ThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			haveNewFrameData = true;
			updatedFrameCount += m_ThreadData[i].UpdatedFrameCount;
			m_ThreadData[i].UpdatedFrameCount = 0;
			contentSize.cx += m_ThreadData[i].ContentSize.cx;
			contentSize.cy += m_ThreadData[i].ContentSize.cy;
			break;
		}
	}

	if (!haveNewFrameData) {
		return DXGI_ERROR_WAIT_TIMEOUT;
	}
	// We have a new frame so try and process it
	// Try to acquire keyed mutex in order to access shared surface
	HRESULT hr = m_KeyMutex->AcquireSync(1, timeoutMillis);
	if (FAILED(hr))
	{
		return DXGI_ERROR_WAIT_TIMEOUT;
	}
	ReleaseKeyedMutexOnExit releaseMutex(m_KeyMutex, 0);

	ID3D11Texture2D *pDesktopFrame = nullptr;
	D3D11_TEXTURE2D_DESC desc;
	m_SharedSurf->GetDesc(&desc);
	desc.MiscFlags = 0;

	RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pDesktopFrame));
	m_ImmediateContext->CopyResource(pDesktopFrame, m_SharedSurf);

	QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);

	frame->Frame = pDesktopFrame;
	frame->PtrInfo = nullptr;
	frame->UpdateCount = updatedFrameCount;
	frame->Timestamp = m_LastAcquiredFrameTimeStamp;
	frame->ContentSize = contentSize;
	return hr;
}


HRESULT graphics_capture::StopCapture()
{
	if (!SetEvent(m_TerminateThreadsEvent)) {
		LOG_ERROR("Could not terminate Windows Graphics Capture thread");
		return E_FAIL;
	}
	WaitForThreadTermination();
	return S_OK;
}


SIZE graphics_capture::FrameSize()
{
	SIZE size;
	size.cx = m_OutputRect.right - m_OutputRect.left;
	size.cy = m_OutputRect.bottom - m_OutputRect.top;
	return size;
}

//
// Waits infinitely for all spawned threads to terminate
//
void graphics_capture::WaitForThreadTermination()
{
	if (m_ThreadCount != 0)
	{
		WaitForMultipleObjectsEx(m_ThreadCount, m_ThreadHandles, TRUE, INFINITE, FALSE);
	}
}


//
// Entry point for new capture threads
//
DWORD WINAPI GCProc(_In_ void* Param)
{
	HRESULT hr = S_OK;

	// Classes
	graphics_manager graphicsManager{};
	GraphicsCaptureItem captureItem{ nullptr };
	// D3D objects
	ID3D11Texture2D* SharedSurf = nullptr;
	IDXGIKeyedMutex* KeyMutex = nullptr;

	bool isExpectedError = false;
	bool isUnexpectedError = false;

	// Data passed in from thread creation
	THREAD_DATA* TData = reinterpret_cast<THREAD_DATA*>(Param);
	RECT frameRect{};
	if (TData->OutputWindow != nullptr) {
		captureItem = capture::util::CreateCaptureItemForWindow(TData->OutputWindow);
		frameRect.right = captureItem.Size().Width;
		frameRect.bottom = captureItem.Size().Height;
	}
	else {
		auto pMonitorList = std::make_unique<monitor_list>(false);
		auto monitor = pMonitorList->GetMonitorForDisplayName(TData->OutputMonitor);
		if (!monitor.has_value()) {
			if (pMonitorList->GetCurrentMonitors().size() == 0) {
				LOG_ERROR("Failed to find any monitors to record");
				hr = E_FAIL;
				goto Exit;
			}
			monitor = pMonitorList->GetCurrentMonitors().at(0);
		}
		LOG_INFO(L"Recording monitor %ls using Windows.Graphics.Capture", monitor->DisplayName.c_str());
		captureItem = capture::util::CreateCaptureItemForMonitor(monitor->MonitorHandle);
		frameRect = monitor->MonitorRect;
	}
	//This scope must be here for ReleaseOnExit to work.
	{
		// Obtain handle to sync shared Surface
		hr = TData->DxRes.Device->OpenSharedResource(TData->TexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&SharedSurf));
		if (FAILED(hr))
		{
			LOG_ERROR(L"Opening shared texture failed");
			goto Exit;
		}
		ReleaseOnExit releaseSharedSurf(SharedSurf);
		hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&KeyMutex));
		if (FAILED(hr))
		{
			LOG_ERROR(L"Failed to get keyed mutex interface in spawned thread");
			goto Exit;
		}
		ReleaseOnExit releaseMutex(KeyMutex);
		// Initialize graphics manager.
		hr = graphicsManager.Initialize(&TData->DxRes, captureItem, TData->IsCursorCaptureEnabled, DirectXPixelFormat::B8G8R8A8UIntNormalized);

		if (FAILED(hr))
		{
			goto Exit;
		}
		D3D11_TEXTURE2D_DESC outputDesc;
		SharedSurf->GetDesc(&outputDesc);

		// Main capture loop
		bool WaitToProcessCurrentFrame = false;
		GRAPHICS_FRAME_DATA CurrentData{};

		while (true)
		{
			if (WaitForSingleObjectEx(TData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
				hr = S_OK;
				break;
			}

			if (!WaitToProcessCurrentFrame)
			{
				// Get new frame from Windows Graphics Capture.
				hr = graphicsManager.GetFrame(&CurrentData);
				if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
					continue;
				}
				else if (FAILED(hr)) {
					break;
				}
			}
			/*To correctly resize recordings, the window contentsize must be returned for window recordings,
			and the output dimensions of the shared surface for monitor recording.*/
			if (TData->OutputWindow) {
				TData->ContentSize = CurrentData.ContentSize;
			}
			else {
				TData->ContentSize.cx = outputDesc.Width;
				TData->ContentSize.cy = outputDesc.Height;
			}

			// We have a new frame so try and process it
			// Try to acquire keyed mutex in order to access shared surface
			hr = KeyMutex->AcquireSync(0, 10);
			if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
			{
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

			ReleaseKeyedMutexOnExit releaseMutex(KeyMutex, 1);

			// We can now process the current frame
			WaitToProcessCurrentFrame = false;

			// Process new frame
			hr = graphicsManager.ProcessFrame(&CurrentData, SharedSurf, TData->OffsetX, TData->OffsetY, frameRect);
			if (FAILED(hr))
			{
				KeyMutex->ReleaseSync(1);
				break;
			}
			TData->UpdatedFrameCount += 1;
			// Release acquired keyed mutex
			hr = KeyMutex->ReleaseSync(1);
			if (FAILED(hr))
			{
				LOG_ERROR(L"Unexpected error releasing the keyed mutex");
				break;
			}

			QueryPerformanceCounter(&TData->LastUpdateTimeStamp);
		}
	}
Exit:
	graphicsManager.Close();

	TData->ThreadResult = hr;

	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Error in Windows Graphics Capture, aborting: %s", err.ErrorMessage());

	}

	if (isExpectedError) {
		SetEvent(TData->ExpectedErrorEvent);
	}
	else if (isUnexpectedError) {
		SetEvent(TData->UnexpectedErrorEvent);
	}

	return 0;
}

#include "graphics_capture.h"
#include "graphics_capture.util.h"
#include <atlbase.h>
#include "utilities.h"
#include "graphics_manager.h"
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

graphics_capture::graphics_capture(_In_ ID3D11Device* pDevice, _In_ ID3D11DeviceContext *pDeviceContext, _In_ bool isCursorCaptureEnabled) :capture_base(pDevice, pDeviceContext)
{
	m_isCursorCaptureEnabled = isCursorCaptureEnabled;
}

graphics_capture::~graphics_capture()
{

}


HRESULT graphics_capture::StartCapture(std::vector<std::wstring> const &sources, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent)
{
	ResetEvent(m_TerminateThreadsEvent);
	std::vector<std::wstring> Outputs;
	std::vector<SIZE> Offsets;
	HRESULT hr = CreateSharedSurf(sources, &Outputs, &Offsets, &m_OutputRect);
	if (FAILED(hr)) {
		return hr;
	}
	m_ThreadCount = (UINT)Outputs.size();
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
		std::wstring outputName = Outputs.at(i);
		m_ThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_ThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_ThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_ThreadData[i].TexSharedHandle = sharedHandle;

		m_ThreadData[i].RecordingSource = new RECORDING_SOURCE_DATA();
		m_ThreadData[i].RecordingSource->OutputMonitor = outputName;
		m_ThreadData[i].RecordingSource->OffsetX = m_OutputRect.left + Offsets.at(i).cx;
		m_ThreadData[i].RecordingSource->OffsetY = m_OutputRect.top + Offsets.at(i).cy;
		m_ThreadData[i].RecordingSource->PtrInfo = &m_PtrInfo;
		m_ThreadData[i].RecordingSource->IsCursorCaptureEnabled = m_isCursorCaptureEnabled;

		RtlZeroMemory(&m_ThreadData[i].RecordingSource->DxRes, sizeof(DX_RESOURCES));
		HRESULT hr = InitializeDx(&m_ThreadData[i].RecordingSource->DxRes);
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
		m_ThreadData[i].TexSharedHandle = sharedHandle;

		m_ThreadData[i].RecordingSource->OutputWindow = windowhandle;
		m_ThreadData[i].RecordingSource = new RECORDING_SOURCE_DATA();
		m_ThreadData[i].RecordingSource->OutputWindow = windowhandle;
		m_ThreadData[i].RecordingSource->OffsetX = m_OutputRect.left;
		m_ThreadData[i].RecordingSource->OffsetY = m_OutputRect.top;
		m_ThreadData[i].RecordingSource->PtrInfo = &m_PtrInfo;
		m_ThreadData[i].RecordingSource->IsCursorCaptureEnabled = m_isCursorCaptureEnabled;

		RtlZeroMemory(&m_ThreadData[i].RecordingSource->DxRes, sizeof(DX_RESOURCES));
		HRESULT hr = InitializeDx(&m_ThreadData[i].RecordingSource->DxRes);
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
// Create shared texture
//
HRESULT graphics_capture::CreateSharedSurf(_In_ HWND windowhandle, _Out_ RECT* pDeskBounds)
{
	IDXGIOutput* DxgiOutput = nullptr;

	// Figure out right dimensions for full size desktop texture and # of outputs to capture
	auto captureItem = capture::util::CreateCaptureItemForWindow(windowhandle);
	pDeskBounds->left = 0;
	pDeskBounds->top = 0;
	pDeskBounds->right = captureItem.Size().Width;
	pDeskBounds->bottom = captureItem.Size().Height;

	return capture_base::CreateSharedSurf(*pDeskBounds);
}

//
// Create shared texture
//
HRESULT graphics_capture::CreateSharedSurf(_In_ std::vector<std::wstring> outputs, _Out_ std::vector<std::wstring> *pOutputs, _Out_ std::vector<SIZE> *pOffsets, _Out_ RECT* pDeskBounds)
{
	// Figure out right dimensions for full size desktop texture and # of outputs to duplicate
	std::vector<DXGI_OUTPUT_DESC> outputDescs{};
	HRESULT hr = GetOutputDescsForDeviceNames(outputs, &outputDescs);
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to get output descs for selected devices");
		return hr;
	}

	if (outputDescs.size() == 0)
	{
		// We could not find any outputs
		return E_FAIL;
	}

	std::sort(outputDescs.begin(), outputDescs.end(), compareOutputDesc);
	std::vector<RECT> outputRects{};
	for each (DXGI_OUTPUT_DESC desc in outputDescs)
	{
		outputRects.push_back(desc.DesktopCoordinates);
	}

	std::vector<SIZE> outputOffsets{};
	GetCombinedRects(outputRects, pDeskBounds, &outputOffsets);

	pDeskBounds = &MakeRectEven(*pDeskBounds);
	std::vector<std::wstring> mergedOutputs{};
	for each (DXGI_OUTPUT_DESC desc in outputDescs)
	{
		mergedOutputs.push_back(desc.DeviceName);
	}
	// Set created outputs
	*pOutputs = mergedOutputs;
	*pOffsets = outputOffsets;

	return capture_base::CreateSharedSurf(*pDeskBounds);
}



HRESULT graphics_capture::AcquireNextFrame(_In_  DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame)
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

	bool haveNewFrameData = false;
	int updatedFrameCount = 0;
	SIZE contentSize{};

	for (UINT i = 0; i < m_ThreadCount; ++i)
	{
		if (m_ThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			haveNewFrameData = true;
			updatedFrameCount += m_ThreadData[i].UpdatedFrameCount;
			m_ThreadData[i].UpdatedFrameCount = 0;
			contentSize.cx += m_ThreadData[i].RecordingSource->ContentSize.cx;
			contentSize.cy += m_ThreadData[i].RecordingSource->ContentSize.cy;
			break;
		}
	}

	if (!haveNewFrameData) {
		return DXGI_ERROR_WAIT_TIMEOUT;
	}

	for (UINT i = 0; i < m_ThreadCount; ++i)
	{
		if (m_ThreadData[i].RecordingSource) {
			if (m_ThreadData[i].RecordingSource->TotalUpdatedFrameCount == 0) {
				//If any of the recordings have not yet written a frame, we return and wait for them.
				return DXGI_ERROR_WAIT_TIMEOUT;
			}
		}
	}

	ID3D11Texture2D *pDesktopFrame = nullptr;
	D3D11_TEXTURE2D_DESC desc;
	m_SharedSurf->GetDesc(&desc);
	desc.MiscFlags = 0;

	RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pDesktopFrame));
	m_DeviceContext->CopyResource(pDesktopFrame, m_SharedSurf);

	QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);

	pFrame->Frame = pDesktopFrame;
	pFrame->UpdateCount = updatedFrameCount;
	pFrame->Timestamp = m_LastAcquiredFrameTimeStamp;
	pFrame->ContentSize = contentSize;
	pFrame->PtrInfo = &m_PtrInfo;
	return hr;
}

//
// Entry point for new capture threads
//
DWORD WINAPI GCProc(_In_ void* Param)
{
	HRESULT hr = S_OK;

	// Classes
	mouse_pointer pMousePointer{};
	graphics_manager graphicsManager{};
	GraphicsCaptureItem captureItem{ nullptr };
	// D3D objects
	ID3D11Texture2D* SharedSurf = nullptr;
	IDXGIKeyedMutex* KeyMutex = nullptr;

	bool isExpectedError = false;
	bool isUnexpectedError = false;

	// Data passed in from thread creation
	THREAD_DATA* pData = reinterpret_cast<THREAD_DATA*>(Param);
	RECORDING_SOURCE_DATA *pSource = pData->RecordingSource;

	RECT frameRect{};
	if (pData->RecordingSource->OutputWindow != nullptr) {
		captureItem = capture::util::CreateCaptureItemForWindow(pSource->OutputWindow);
		frameRect.right = captureItem.Size().Width;
		frameRect.bottom = captureItem.Size().Height;
	}
	else {
		CComPtr<IDXGIOutput> output = nullptr;
		HRESULT hr = GetOutputForDeviceName(pSource->OutputMonitor, &output);
		if (FAILED(hr)) {
			GetMainOutput(&output);
			if (!output) {
				LOG_ERROR("Failed to find any monitors to record");
				hr = E_FAIL;
				goto Exit;
			}
		}
		DXGI_OUTPUT_DESC outputDesc;
		output->GetDesc(&outputDesc);
		LOG_INFO(L"Recording monitor %ls using Windows.Graphics.Capture", outputDesc.DeviceName);
		captureItem = capture::util::CreateCaptureItemForMonitor(outputDesc.Monitor);
		frameRect = outputDesc.DesktopCoordinates;
	}
	pMousePointer.Initialize(pSource->DxRes.Context, pSource->DxRes.Device);
	//This scope must be here for ReleaseOnExit to work.
	{
		// Obtain handle to sync shared Surface
		hr = pSource->DxRes.Device->OpenSharedResource(pData->TexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&SharedSurf));
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
		hr = graphicsManager.Initialize(&pSource->DxRes, captureItem, pSource->IsCursorCaptureEnabled, DirectXPixelFormat::B8G8R8A8UIntNormalized);

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
			if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
				hr = S_OK;
				break;
			}

			if (!WaitToProcessCurrentFrame)
			{
				// Get new frame from Windows Graphics Capture.
				hr = graphicsManager.GetFrame(&CurrentData);
				if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
					Sleep(1);
					continue;
				}
				else if (FAILED(hr)) {
					break;
				}
			}
			/*To correctly resize recordings, the window contentsize must be returned for window recordings,
			and the output dimensions of the shared surface for monitor recording.*/
			if (pSource->OutputWindow) {
				pSource->ContentSize = CurrentData.ContentSize;
			}
			else {
				pSource->ContentSize.cx = outputDesc.Width;
				pSource->ContentSize.cy = outputDesc.Height;
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

			// Get mouse info. Windows Graphics Capture includes the mouse cursor on the texture, so we only get the positioning info for mouse click draws.
			hr = pMousePointer.GetMouse(pSource->PtrInfo, false, pSource->OffsetX, pSource->OffsetY);

			// Process new frame
			hr = graphicsManager.ProcessFrame(&CurrentData, SharedSurf, pSource->OffsetX, pSource->OffsetY, frameRect);
			if (FAILED(hr))
			{
				KeyMutex->ReleaseSync(1);
				break;
			}
			pData->UpdatedFrameCount++;
			pSource->TotalUpdatedFrameCount++;
			// Release acquired keyed mutex
			hr = KeyMutex->ReleaseSync(1);
			if (FAILED(hr))
			{
				LOG_ERROR(L"Unexpected error releasing the keyed mutex");
				break;
			}

			QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
		}
	}
Exit:
	graphicsManager.Close();

	pData->ThreadResult = hr;

	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Error in Windows Graphics Capture, aborting: %s", err.ErrorMessage());

	}

	if (isExpectedError) {
		SetEvent(pData->ExpectedErrorEvent);
	}
	else if (isUnexpectedError) {
		SetEvent(pData->UnexpectedErrorEvent);
	}

	return 0;
}

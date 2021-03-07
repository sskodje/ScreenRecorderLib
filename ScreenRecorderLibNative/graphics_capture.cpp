#include "graphics_capture.h"
#include "graphics_capture.util.h"

#include "utilities.h"
#include "cleanup.h"
#include "graphics_manager.h"

using namespace capture::util;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;

static DWORD WINAPI CaptureThreadProc(_In_ void* Param);

graphics_capture::graphics_capture(_In_ ID3D11Device* pDevice, _In_ ID3D11DeviceContext *pDeviceContext) :
	capture_base(pDevice, pDeviceContext)
{

}

graphics_capture::~graphics_capture()
{

}

HRESULT graphics_capture::StartCapture(_In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent)
{
	return capture_base::StartCapture(CaptureThreadProc, sources, overlays, hUnexpectedErrorEvent, hExpectedErrorEvent);
}

//
// Entry point for new capture threads
//
DWORD WINAPI CaptureThreadProc(_In_ void* Param)
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
	CAPTURE_THREAD_DATA* pData = reinterpret_cast<CAPTURE_THREAD_DATA*>(Param);
	RECORDING_SOURCE_DATA *pSource = pData->RecordingSource;

	RtlZeroMemory(&pData->ContentFrameRect, sizeof(pData->ContentFrameRect));
	if (pData->RecordingSource->OutputWindow != nullptr) {
		captureItem = capture::util::CreateCaptureItemForWindow(pSource->OutputWindow);
		pData->ContentFrameRect.right = captureItem.Size().Width;
		pData->ContentFrameRect.bottom = captureItem.Size().Height;
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
		pData->ContentFrameRect = outputDesc.DesktopCoordinates;
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
				pData->ContentFrameRect.right = CurrentData.ContentSize.cx + pData->ContentFrameRect.left;
				pData->ContentFrameRect.bottom = CurrentData.ContentSize.cy + pData->ContentFrameRect.top;
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
			hr = pMousePointer.GetMouse(pData->PtrInfo, false, pSource->OffsetX, pSource->OffsetY);

			// Process new frame
			hr = graphicsManager.ProcessFrame(&CurrentData, SharedSurf, pSource->OffsetX, pSource->OffsetY, pData->ContentFrameRect);
			if (FAILED(hr))
			{
				KeyMutex->ReleaseSync(1);
				break;
			}
			pData->UpdatedFrameCountSinceLastWrite++;
			pData->TotalUpdatedFrameCount++;
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
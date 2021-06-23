#include "WindowsGraphicsCapture.h"
#include "WindowsGraphicsCapture.util.h"

#include "Util.h"
#include "Cleanup.h"
#include "WindowsGraphicsManager.h"

using namespace Graphics::Capture::Util;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;

static DWORD WINAPI CaptureThreadProc(_In_ void *Param);

WindowsGraphicsCapture::WindowsGraphicsCapture() :
	ScreenCaptureBase()
{

}

WindowsGraphicsCapture::~WindowsGraphicsCapture()
{

}

RECT WindowsGraphicsCapture::GetOutputRect()
{
	if (IsSingleWindowCapture()) {
		while (IsCapturing() && !IsInitialFrameWriteComplete()) {
			Sleep(1);
		}
		return GetContentRect();
	}
	else {
		return m_OutputRect;
	}
}

LPTHREAD_START_ROUTINE WindowsGraphicsCapture::GetCaptureThreadProc()
{
	return CaptureThreadProc;
}

//
// Entry point for new capture threads
//
DWORD WINAPI CaptureThreadProc(_In_ void *Param)
{
	HRESULT hr = S_OK;

	// Classes
	MouseManager pMouseManager{};
	WindowsGraphicsManager graphicsManager{};
	GraphicsCaptureItem captureItem{ nullptr };
	// D3D objects
	ID3D11Texture2D *SharedSurf = nullptr;
	IDXGIKeyedMutex *KeyMutex = nullptr;

	bool isExpectedError = false;
	bool isUnexpectedError = false;

	// Data passed in from thread creation
	CAPTURE_THREAD_DATA *pData = reinterpret_cast<CAPTURE_THREAD_DATA *>(Param);
	RECORDING_SOURCE_DATA *pSource = pData->RecordingSource;

	RtlZeroMemory(&pData->ContentFrameRect, sizeof(pData->ContentFrameRect));
	if (pData->RecordingSource->WindowHandle != nullptr) {
		captureItem = CreateCaptureItemForWindow(pSource->WindowHandle);
		pData->ContentFrameRect.right = captureItem.Size().Width;
		pData->ContentFrameRect.bottom = captureItem.Size().Height;
	}
	else {
		CComPtr<IDXGIOutput> output = nullptr;
		HRESULT hr = GetOutputForDeviceName(pSource->CaptureDevice, &output);
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
		captureItem = CreateCaptureItemForMonitor(outputDesc.Monitor);
		pData->ContentFrameRect = outputDesc.DesktopCoordinates;
	}
	//This scope must be here for ReleaseOnExit to work.
	{
		pMouseManager.Initialize(pSource->DxRes.Context, pSource->DxRes.Device, std::make_shared<MOUSE_OPTIONS>());
		// Obtain handle to sync shared Surface
		hr = pSource->DxRes.Device->OpenSharedResource(pData->TexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&SharedSurf));
		if (FAILED(hr))
		{
			LOG_ERROR(L"Opening shared texture failed");
			goto Exit;
		}
		ReleaseOnExit releaseSharedSurf(SharedSurf);
		hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&KeyMutex));
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
					if (!CurrentData.IsIconic && pSource->WindowHandle && IsIconic(pSource->WindowHandle)) {
						CurrentData.IsIconic = true;
						LOG_INFO("Recorded window is minimized");
					}
					else {
						Sleep(1);
						continue;
					}
				}
				else if (FAILED(hr)) {
					break;
				}
				else if (SUCCEEDED(hr)) {
					if (CurrentData.IsIconic) {
						CurrentData.IsIconic = false;
						LOG_INFO("Recorded window is restored and no longer minimized");
					}
				}
			}
			/*To correctly resize recordings, the window contentsize must be returned for window recordings,
			and the output dimensions of the shared surface for monitor recording.*/
			if (pSource->WindowHandle) {
				pData->ContentFrameRect.right = CurrentData.ContentSize.cx + pData->ContentFrameRect.left;
				pData->ContentFrameRect.bottom = CurrentData.ContentSize.cy + pData->ContentFrameRect.top;
			}
			{
				MeasureExecutionTime measure(L"Duplication CaptureThreadProc wait for sync");
				// We have a new frame so try and process it
				// Try to acquire keyed mutex in order to access shared surface
				hr = KeyMutex->AcquireSync(0, 10);
			}
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
			hr = pMouseManager.GetMouse(pData->PtrInfo, false, pSource->OffsetX, pSource->OffsetY);

			// Process new frame
			hr = graphicsManager.ProcessFrame(&CurrentData, SharedSurf, pSource->OffsetX, pSource->OffsetY, pData->ContentFrameRect);
			if (FAILED(hr))
			{
				break;
			}
			pData->UpdatedFrameCountSinceLastWrite++;
			pData->TotalUpdatedFrameCount++;

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
#include "DesktopDuplicationCapture.h"
#include "DesktopDuplicationManager.h"
#include <chrono>
#include "Cleanup.h"
#include "MousePointer.h"
using namespace std::chrono;
DWORD WINAPI CaptureThreadProc(_In_ void *Param);

duplication_capture::duplication_capture() :
	ScreenCaptureBase()
{

}

duplication_capture::~duplication_capture()
{

}

LPTHREAD_START_ROUTINE duplication_capture::GetCaptureThreadProc()
{
	return CaptureThreadProc;
}

//
// Entry point for new duplication threads
//
DWORD WINAPI CaptureThreadProc(_In_ void *Param)
{
	HRESULT hr = S_OK;

	// Classes
	DesktopDuplicationManager pDuplicationManager{};
	MousePointer pMousePointer{};

	// D3D objects
	ID3D11Texture2D *SharedSurf = nullptr;
	IDXGIKeyedMutex *KeyMutex = nullptr;

	bool isExpectedError = false;
	bool isUnexpectedError = false;

	// Data passed in from thread creation
	CAPTURE_THREAD_DATA *pData = reinterpret_cast<CAPTURE_THREAD_DATA *>(Param);

	// Get desktop
	HDESK CurrentDesktop = nullptr;
	CurrentDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
	if (!CurrentDesktop)
	{
		// We do not have access to the desktop so request a retry
		isExpectedError = true;
		hr = E_ACCESSDENIED;
		goto Exit;
	}

	// Attach desktop to this thread
	bool DesktopAttached = SetThreadDesktop(CurrentDesktop) != 0;
	CloseDesktop(CurrentDesktop);
	CurrentDesktop = nullptr;
	if (!DesktopAttached)
	{
		// We do not have access to the desktop so request a retry
		isExpectedError = TRUE;
		hr = E_ACCESSDENIED;
		goto Exit;
	}
	RECORDING_SOURCE_DATA *pSource = pData->RecordingSource;
	pMousePointer.Initialize(pSource->DxRes.Context, pSource->DxRes.Device);
	//This scope must be here for ReleaseOnExit to work.
	{
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
		// Make duplication manager
		hr = pDuplicationManager.Initialize(&pSource->DxRes, pSource->CaptureDevice);

		if (FAILED(hr))
		{
			goto Exit;
		}

		// Get output description
		DXGI_OUTPUT_DESC DesktopDesc;
		RtlZeroMemory(&DesktopDesc, sizeof(DXGI_OUTPUT_DESC));
		pDuplicationManager.GetOutputDesc(&DesktopDesc);
		pData->ContentFrameRect = DesktopDesc.DesktopCoordinates;
		// Main duplication loop
		bool WaitToProcessCurrentFrame = false;
		DUPL_FRAME_DATA CurrentData{};

		while (true)
		{
			if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
				hr = S_OK;
				break;
			}

			if (!WaitToProcessCurrentFrame)
			{
				// Get new frame from desktop duplication
				hr = pDuplicationManager.GetFrame(&CurrentData);
				if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
					continue;
				}
				else if (FAILED(hr)) {
					break;
				}
			}
			{
				MeasureExecutionTime measure(L"Duplication CaptureThreadProc wait for sync");
				// We have a new frame so try and process it
				// Try to acquire keyed mutex in order to access shared surface
				hr = KeyMutex->AcquireSync(0, 10);
			}
			if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
			{
				LOG_TRACE(L"CaptureThreadProc shared surface is busy, retrying..");
				// Can't use shared surface right now, try again later
				WaitToProcessCurrentFrame = true;
				continue;
			}
			else if (FAILED(hr))
			{
				// Generic unknown failure
				LOG_ERROR(L"Unexpected error acquiring KeyMutex");
				pDuplicationManager.ReleaseFrame();
				break;
			}


			ReleaseDuplicationManagerFrameOnExit releaseFrame(&pDuplicationManager);
			ReleaseKeyedMutexOnExit releaseMutex(KeyMutex, 1);

			// We can now process the current frame
			WaitToProcessCurrentFrame = false;

			// Get mouse info
			hr = pMousePointer.GetMouse(pData->PtrInfo, &(CurrentData.FrameInfo), DesktopDesc.DesktopCoordinates, pDuplicationManager.GetOutputDuplication(), pSource->OffsetX, pSource->OffsetY);
			if (FAILED(hr))
			{
				break;
			}

			// Process new frame
			hr = pDuplicationManager.ProcessFrame(&CurrentData, SharedSurf, pSource->OffsetX, pSource->OffsetY, &DesktopDesc);
			if (FAILED(hr))
			{
				break;
			}
			if (CurrentData.FrameInfo.AccumulatedFrames > 0) {
				pData->UpdatedFrameCountSinceLastWrite++;
				pData->TotalUpdatedFrameCount++;
			}
			if (CurrentData.FrameInfo.LastPresentTime.QuadPart > CurrentData.FrameInfo.LastMouseUpdateTime.QuadPart) {
				pData->LastUpdateTimeStamp = CurrentData.FrameInfo.LastPresentTime;
			}
			else {
				pData->LastUpdateTimeStamp = CurrentData.FrameInfo.LastMouseUpdateTime;
			}
		}
	}
Exit:

	pData->ThreadResult = hr;

	if (FAILED(hr))
	{
		_com_error err(hr);
		switch (hr)
		{
		case DXGI_ERROR_DEVICE_REMOVED:
		case DXGI_ERROR_DEVICE_RESET:
			LOG_ERROR(L"Display device unavailable: %s", err.ErrorMessage());
			isUnexpectedError = true;
			break;
		case E_ACCESSDENIED:
		case DXGI_ERROR_MODE_CHANGE_IN_PROGRESS:
		case DXGI_ERROR_SESSION_DISCONNECTED:
			//case DXGI_ERROR_INVALID_CALL:
		case DXGI_ERROR_ACCESS_LOST:
			//Access to video output is denied, probably due to DRM, screen saver, desktop is switching, fullscreen application is launching, or similar.
			//We continue the recording, and instead of desktop texture just add a blank texture instead.
			isExpectedError = true;
			LOG_WARN(L"Desktop duplication temporarily unavailable: hr = 0x%08x, error = %s", hr, err.ErrorMessage());
			break;
		case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
			LOG_ERROR(L"Error reinitializing desktop duplication with DXGI_ERROR_NOT_CURRENTLY_AVAILABLE. This means DXGI reached the limit on the maximum number of concurrent duplication applications (default of four). Therefore, the calling application cannot create any desktop duplication interfaces until the other applications close");
			isUnexpectedError = true;
			break;
		default:
			//Unexpected error, return.
			LOG_ERROR(L"Error reinitializing desktop duplication with unexpected error, aborting: %s", err.ErrorMessage());
			isUnexpectedError = true;
			break;
		}
	}

	if (isExpectedError) {
		SetEvent(pData->ExpectedErrorEvent);
	}
	else if (isUnexpectedError) {
		SetEvent(pData->UnexpectedErrorEvent);
	}

	return 0;
}
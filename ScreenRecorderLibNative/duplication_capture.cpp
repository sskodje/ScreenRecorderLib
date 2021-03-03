#include "duplication_capture.h"
#include "duplication_manager.h"

#include "cleanup.h"
#include "mouse_pointer.h"
#include "video_capture.h"

DWORD WINAPI DDProc(_In_ void* Param);

duplication_capture::duplication_capture(_In_ ID3D11Device* pDevice, _In_ ID3D11DeviceContext *pDeviceContext) :capture_base(pDevice, pDeviceContext)
{

}

duplication_capture::~duplication_capture()
{

}

//
// Start up threads for DDA
//
HRESULT duplication_capture::StartCapture(_In_ std::vector<std::wstring> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent)
{
	ResetEvent(m_TerminateThreadsEvent);
	std::map<std::wstring, SIZE> CreatedOutputsWithOffsets;
	HRESULT hr = CreateSharedSurf(sources, &CreatedOutputsWithOffsets, &m_OutputRect);
	if (FAILED(hr)) {
		return hr;
	}
	m_ThreadCount = (UINT)(CreatedOutputsWithOffsets.size() + overlays.size());
	m_ThreadHandles = new (std::nothrow) HANDLE[m_ThreadCount]{};
	m_ThreadData = new (std::nothrow) THREAD_DATA[m_ThreadCount]{};
	if (!m_ThreadHandles || !m_ThreadData)
	{
		return E_OUTOFMEMORY;
	}
	HANDLE sharedHandle = GetSharedHandle();
	// Create appropriate # of threads for duplication
	int i = 0;
	for each (auto &const pair in CreatedOutputsWithOffsets)
	{
		if (i < m_ThreadCount) {
			std::wstring outputName = pair.first;
			SIZE offset = pair.second;
			m_ThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
			m_ThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
			m_ThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
			m_ThreadData[i].TexSharedHandle = sharedHandle;

			m_ThreadData[i].RecordingSource = new RECORDING_SOURCE_DATA();
			m_ThreadData[i].RecordingSource->OutputMonitor = outputName;
			m_ThreadData[i].RecordingSource->OffsetX = m_OutputRect.left + offset.cx;
			m_ThreadData[i].RecordingSource->OffsetY = m_OutputRect.top + offset.cy;
			m_ThreadData[i].RecordingSource->PtrInfo = &m_PtrInfo;

			RtlZeroMemory(&m_ThreadData[i].RecordingSource->DxRes, sizeof(DX_RESOURCES));
			HRESULT hr = InitializeDx(&m_ThreadData[i].RecordingSource->DxRes);
			if (FAILED(hr))
			{
				return hr;
			}

			DWORD ThreadId;
			m_ThreadHandles[i] = CreateThread(nullptr, 0, DDProc, &m_ThreadData[i], 0, &ThreadId);
			if (m_ThreadHandles[i] == nullptr)
			{
				return E_FAIL;
			}
			i++;
		}
	}
	for each (RECORDING_OVERLAY overlay in overlays)
	{
		if (i < m_ThreadCount) {
			m_ThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
			m_ThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
			m_ThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
			m_ThreadData[i].TexSharedHandle = sharedHandle;

			m_ThreadData[i].RecordingOverlay = new RECORDING_OVERLAY_DATA();
			m_ThreadData[i].RecordingOverlay->CaptureDevice = overlay.CaptureDevice;
			m_ThreadData[i].RecordingOverlay->Position = overlay.Position;
			m_ThreadData[i].RecordingOverlay->Size = overlay.Size;
			m_ThreadData[i].RecordingOverlay->Type = overlay.Type;
			m_ThreadData[i].RecordingOverlay->FrameInfo = new FRAME_INFO();
			RtlZeroMemory(&m_ThreadData[i].RecordingOverlay->DxRes, sizeof(DX_RESOURCES));
			HRESULT hr = InitializeDx(&m_ThreadData[i].RecordingOverlay->DxRes);
			if (FAILED(hr))
			{
				return hr;
			}

			DWORD ThreadId;
			m_ThreadHandles[i] = CreateThread(nullptr, 0, OverlayProc, &m_ThreadData[i], 0, &ThreadId);
			if (m_ThreadHandles[i] == nullptr)
			{
				return E_FAIL;
			}
			i++;
		}
	}
	return S_OK;
}

HRESULT duplication_capture::AddOverlaysToTexture(_In_ ID3D11Texture2D * bgTexture)
{
	for (UINT i = 0; i < m_ThreadCount; ++i)
	{
		if (m_ThreadData[i].RecordingOverlay) {
			RECORDING_OVERLAY_DATA *pOverlay = m_ThreadData[i].RecordingOverlay;
			if (pOverlay->FrameInfo) {
				FRAME_INFO *frameInfo = pOverlay->FrameInfo;
				//Create new frame
				D3D11_TEXTURE2D_DESC desc{};
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				desc.SampleDesc.Count = 1;
				desc.SampleDesc.Quality = 0;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_RENDER_TARGET;
				desc.CPUAccessFlags = 0;
				desc.MiscFlags = 0;
				desc.Width = frameInfo->Width;
				desc.Height = frameInfo->Height;

				D3D11_SUBRESOURCE_DATA initData = { 0 };
				initData.pSysMem = frameInfo->PtrFrameBuffer;
				initData.SysMemPitch = abs(frameInfo->Stride);
				initData.SysMemSlicePitch = 0;

				CComPtr<ID3D11Texture2D> pOverlayTexture;
				HRESULT hr = m_Device->CreateTexture2D(&desc, &initData, &pOverlayTexture);
				if (SUCCEEDED(hr)) {
					D3D11_BOX Box{};
					// Copy back to shared surface
					Box.right = desc.Width;
					Box.bottom = desc.Height;
					Box.back = 1;
					m_DeviceContext->CopySubresourceRegion(bgTexture, 0, 0, 0, 0, pOverlayTexture, 0, &Box);
				}
			}
		}
	}
	return S_OK;
}

//
// Create shared texture
//
HRESULT duplication_capture::CreateSharedSurf(_In_ std::vector<std::wstring> sources, _Out_ std::map<std::wstring, SIZE> *pCreatedOutputsWithOffsets, _Out_ RECT *pDeskBounds)
{
	// Set initial values so that we always catch the right coordinates
	pDeskBounds->left = INT_MAX;
	pDeskBounds->right = INT_MIN;
	pDeskBounds->top = INT_MAX;
	pDeskBounds->bottom = INT_MIN;

	// Figure out right dimensions for full size desktop texture and # of outputs to duplicate
	std::vector<DXGI_OUTPUT_DESC> outputDescs{};
	HRESULT hr = GetOutputDescsForDeviceNames(sources, &outputDescs);
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
	std::map<std::wstring, SIZE> createdOutputs{};
	for (int i = 0; i < outputDescs.size(); i++)
	{
		DXGI_OUTPUT_DESC desc = outputDescs[i];
		createdOutputs.insert(std::pair<std::wstring, SIZE>(desc.DeviceName, outputOffsets.at(i)));
	}
	// Set created outputs
	*pCreatedOutputsWithOffsets = createdOutputs;
	return capture_base::CreateSharedSurf(*pDeskBounds);
}

HRESULT duplication_capture::AcquireNextFrame(_In_  DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame)
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

	for (UINT i = 0; i < m_ThreadCount; ++i)
	{
		if (m_ThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			haveNewFrameData = true;
			updatedFrameCount += m_ThreadData[i].UpdatedFrameCount;
			m_ThreadData[i].UpdatedFrameCount = 0;
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

	RETURN_ON_BAD_HR(hr = AddOverlaysToTexture(pDesktopFrame));

	QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);

	pFrame->Frame = pDesktopFrame;
	pFrame->PtrInfo = &m_PtrInfo;
	pFrame->UpdateCount = updatedFrameCount;
	pFrame->Timestamp = m_LastAcquiredFrameTimeStamp;
	return hr;
}

//
// Entry point for new duplication threads
//
DWORD WINAPI DDProc(_In_ void* Param)
{
	HRESULT hr = S_OK;

	// Classes
	duplication_manager pDuplicationManager{};
	mouse_pointer pMousePointer{};

	// D3D objects
	ID3D11Texture2D* SharedSurf = nullptr;
	IDXGIKeyedMutex* KeyMutex = nullptr;

	bool isExpectedError = false;
	bool isUnexpectedError = false;

	// Data passed in from thread creation
	THREAD_DATA* pData = reinterpret_cast<THREAD_DATA*>(Param);

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
		// Make duplication manager
		hr = pDuplicationManager.Initialize(&pSource->DxRes, pSource->OutputMonitor);

		if (FAILED(hr))
		{
			goto Exit;
		}

		// Get output description
		DXGI_OUTPUT_DESC DesktopDesc;
		RtlZeroMemory(&DesktopDesc, sizeof(DXGI_OUTPUT_DESC));
		pDuplicationManager.GetOutputDesc(&DesktopDesc);
		pSource->ContentSize.cx = DesktopDesc.DesktopCoordinates.right - DesktopDesc.DesktopCoordinates.left;
		pSource->ContentSize.cy = DesktopDesc.DesktopCoordinates.bottom - DesktopDesc.DesktopCoordinates.top;
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
				pDuplicationManager.ReleaseFrame();
				break;
			}
			ReleaseDuplicationManagerFrameOnExit releaseFrame(&pDuplicationManager);
			ReleaseKeyedMutexOnExit releaseMutex(KeyMutex, 1);

			// We can now process the current frame
			WaitToProcessCurrentFrame = false;

			// Get mouse info
			hr = pMousePointer.GetMouse(pSource->PtrInfo, &(CurrentData.FrameInfo), DesktopDesc.DesktopCoordinates, pDuplicationManager.GetOutputDuplication(), pSource->OffsetX, pSource->OffsetY);
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
			pData->UpdatedFrameCount++;
			pSource->TotalUpdatedFrameCount++;
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
#include "duplication_capture.h"
#include "duplication_manager.h"
#include <new>
#include "cleanup.h"
#include "mouse_pointer.h"

static DWORD WINAPI DDProc(_In_ void* Param);

duplication_capture::duplication_capture(_In_ ID3D11Device* pDevice, _In_ ID3D11DeviceContext *pDeviceContext) :
	m_ThreadCount(0),
	m_ThreadHandles(nullptr),
	m_ThreadData(nullptr),
	m_SharedSurf(nullptr),
	m_KeyMutex(nullptr),
	m_LastAcquiredFrameTimeStamp{},
	m_TerminateThreadsEvent(nullptr)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	RtlZeroMemory(&m_PtrInfo, sizeof(m_PtrInfo));

	// Event to tell spawned threads to quit
	m_TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

duplication_capture::~duplication_capture()
{
	Clean();
}

//
// Clean up resources
//
void duplication_capture::Clean()
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

	if (m_ThreadHandles) {
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

	if (m_ThreadData)
	{
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



//
// Start up threads for DDA
//
HRESULT duplication_capture::StartCapture(_In_ std::vector<std::wstring> outputs, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent)
{
	ResetEvent(m_TerminateThreadsEvent);
	std::vector<std::wstring> Outputs;
	std::vector<SIZE> Offsets;
	HRESULT hr = CreateSharedSurf(outputs, &Outputs, &Offsets, &m_OutputRect);
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
	// Create appropriate # of threads for duplication
	for (UINT i = 0; i < m_ThreadCount; i++)
	{
		std::wstring outputName = Outputs.at(i);
		m_ThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_ThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_ThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_ThreadData[i].OutputMonitor = outputName;
		m_ThreadData[i].TexSharedHandle = sharedHandle;
		m_ThreadData[i].OffsetX = m_OutputRect.left + Offsets.at(i).cx;
		m_ThreadData[i].OffsetY = m_OutputRect.top + Offsets.at(i).cy;
		m_ThreadData[i].PtrInfo = &m_PtrInfo;

		RtlZeroMemory(&m_ThreadData[i].DxRes, sizeof(DX_RESOURCES));
		HRESULT hr = InitializeDx(&m_ThreadData[i].DxRes);
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
	}
	return S_OK;
}

HRESULT duplication_capture::StopCapture()
{
	if (!SetEvent(m_TerminateThreadsEvent)) {
		LOG_ERROR("Could not terminate Desktop Duplication capture thread");
		return E_FAIL;
	}
	WaitForThreadTermination();
	return S_OK;
}

//
// Get DX_RESOURCES
//
HRESULT duplication_capture::InitializeDx(_Out_ DX_RESOURCES* pData)
{
	HRESULT hr = S_OK;

	// Driver types supported
	D3D_DRIVER_TYPE DriverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

	// Feature levels supported
	D3D_FEATURE_LEVEL FeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_1
	};
	UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

	D3D_FEATURE_LEVEL FeatureLevel;

	// Create device
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, 0, FeatureLevels, NumFeatureLevels,
			D3D11_SDK_VERSION, &pData->Device, &FeatureLevel, &pData->Context);
		if (SUCCEEDED(hr))
		{
			// Device creation success, no need to loop anymore
			break;
		}
	}
	if (FAILED(hr))
	{
		return  hr;
	}

	// VERTEX shader
	UINT Size = ARRAYSIZE(g_VS);
	hr = pData->Device->CreateVertexShader(g_VS, Size, nullptr, &pData->VertexShader);
	if (FAILED(hr))
	{
		return hr;
	}

	// Input layout
	D3D11_INPUT_ELEMENT_DESC Layout[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	UINT NumElements = ARRAYSIZE(Layout);
	hr = pData->Device->CreateInputLayout(Layout, NumElements, g_VS, Size, &pData->InputLayout);
	if (FAILED(hr))
	{
		return hr;
	}
	pData->Context->IASetInputLayout(pData->InputLayout);

	// Pixel shader
	Size = ARRAYSIZE(g_PS);
	hr = pData->Device->CreatePixelShader(g_PS, Size, nullptr, &pData->PixelShader);
	if (FAILED(hr))
	{
		return hr;
	}

	// Set up sampler
	D3D11_SAMPLER_DESC SampDesc;
	RtlZeroMemory(&SampDesc, sizeof(SampDesc));
	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SampDesc.MinLOD = 0;
	SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	hr = pData->Device->CreateSamplerState(&SampDesc, &pData->SamplerLinear);
	if (FAILED(hr))
	{
		return hr;
	}

	return hr;
}

//
// Create shared texture
//
HRESULT duplication_capture::CreateSharedSurf(_In_ std::vector<std::wstring> outputs, _Out_ std::vector<std::wstring> *pCreatedOutputs, _Out_ std::vector<SIZE> *pCreatedOffsets, _Out_ RECT* pDeskBounds)
{
	// Set initial values so that we always catch the right coordinates
	pDeskBounds->left = INT_MAX;
	pDeskBounds->right = INT_MIN;
	pDeskBounds->top = INT_MAX;
	pDeskBounds->bottom = INT_MIN;

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
	*pCreatedOutputs = mergedOutputs;
	*pCreatedOffsets = outputOffsets;

	// Create shared texture for all duplication threads to draw into
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
// Returns shared handle
//
HANDLE duplication_capture::GetSharedHandle()
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
// Getter for the PTR_INFO structure
//
PTR_INFO* duplication_capture::GetPointerInfo()
{
	return &m_PtrInfo;
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
		if (!m_ThreadData[i].HaveWrittenFirstFrame) {
			//If any of the recordings have not yet written a frame, we return and wait for them.
			return DXGI_ERROR_WAIT_TIMEOUT;
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
	pFrame->PtrInfo = &m_PtrInfo;
	pFrame->UpdateCount = updatedFrameCount;
	pFrame->Timestamp = m_LastAcquiredFrameTimeStamp;
	return hr;
}

//
// Waits infinitely for all spawned threads to terminate
//
void duplication_capture::WaitForThreadTermination()
{
	if (m_ThreadCount != 0)
	{
		WaitForMultipleObjectsEx(m_ThreadCount, m_ThreadHandles, TRUE, INFINITE, FALSE);
	}
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
	THREAD_DATA* TData = reinterpret_cast<THREAD_DATA*>(Param);

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
	pMousePointer.Initialize(TData->DxRes.Context, TData->DxRes.Device);
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
		// Make duplication manager
		hr = pDuplicationManager.Initialize(&TData->DxRes, TData->OutputMonitor);

		if (FAILED(hr))
		{
			goto Exit;
		}

		// Get output description
		DXGI_OUTPUT_DESC DesktopDesc;
		RtlZeroMemory(&DesktopDesc, sizeof(DXGI_OUTPUT_DESC));
		pDuplicationManager.GetOutputDesc(&DesktopDesc);
		TData->ContentSize.cx = DesktopDesc.DesktopCoordinates.right - DesktopDesc.DesktopCoordinates.left;
		TData->ContentSize.cy = DesktopDesc.DesktopCoordinates.bottom - DesktopDesc.DesktopCoordinates.top;
		// Main duplication loop
		bool WaitToProcessCurrentFrame = false;
		DUPL_FRAME_DATA CurrentData{};

		while (true)
		{
			if (WaitForSingleObjectEx(TData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
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
			hr = pMousePointer.GetMouse(TData->PtrInfo, &(CurrentData.FrameInfo), DesktopDesc.DesktopCoordinates, pDuplicationManager.GetOutputDuplication(), TData->OffsetX, TData->OffsetY);
			if (FAILED(hr))
			{
				break;
			}

			// Process new frame
			hr = pDuplicationManager.ProcessFrame(&CurrentData, SharedSurf, TData->OffsetX, TData->OffsetY, &DesktopDesc);
			if (FAILED(hr))
			{
				break;
			}
			TData->UpdatedFrameCount += CurrentData.FrameInfo.AccumulatedFrames;
			if (CurrentData.FrameInfo.AccumulatedFrames > 0) {
				TData->HaveWrittenFirstFrame = true;
			}
			QueryPerformanceCounter(&TData->LastUpdateTimeStamp);
		}
	}
Exit:

	TData->ThreadResult = hr;

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
		SetEvent(TData->ExpectedErrorEvent);
	}
	else if (isUnexpectedError) {
		SetEvent(TData->UnexpectedErrorEvent);
	}

	return 0;
}

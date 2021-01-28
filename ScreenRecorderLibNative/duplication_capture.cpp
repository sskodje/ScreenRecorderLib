#include "duplication_capture.h"
#include "duplication_manager.h"
#include <new>
#include "cleanup.h"
#include "mouse_pointer.h"

DWORD WINAPI DDProc(_In_ void* Param);

duplication_capture::duplication_capture(_In_ ID3D11Device* pDevice, _In_ ID3D11DeviceContext *pDeviceContext) :
	m_ThreadCount(0),
	m_ThreadHandles(nullptr),
	m_ThreadData(nullptr),
	m_SharedSurf(nullptr),
	m_KeyMutex(nullptr),
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

	if (m_ThreadHandles)
	{
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
// Clean up DX_RESOURCES
//
void duplication_capture::CleanDx(_Inout_ DX_RESOURCES* Data)
{
	if (Data->Device)
	{
		Data->Device->Release();
		Data->Device = nullptr;
	}

	if (Data->Context)
	{
		Data->Context->Release();
		Data->Context = nullptr;
	}

	if (Data->VertexShader)
	{
		Data->VertexShader->Release();
		Data->VertexShader = nullptr;
	}

	if (Data->PixelShader)
	{
		Data->PixelShader->Release();
		Data->PixelShader = nullptr;
	}

	if (Data->InputLayout)
	{
		Data->InputLayout->Release();
		Data->InputLayout = nullptr;
	}

	if (Data->SamplerLinear)
	{
		Data->SamplerLinear->Release();
		Data->SamplerLinear = nullptr;
	}
}

//
// Start up threads for DDA
//
HRESULT duplication_capture::StartCapture(std::vector<std::wstring> outputs, HANDLE hUnexpectedErrorEvent, HANDLE hExpectedErrorEvent)
{
	ResetEvent(m_TerminateThreadsEvent);
	std::vector<std::wstring> CreatedOutputs;
	HRESULT hr = CreateSharedSurf(outputs, &CreatedOutputs, &m_OutputRect);
	if (FAILED(hr)) {
		return hr;
	}
	m_ThreadCount = CreatedOutputs.size();
	m_ThreadHandles = new (std::nothrow) HANDLE[m_ThreadCount];
	m_ThreadData = new (std::nothrow) THREAD_DATA[m_ThreadCount];
	if (!m_ThreadHandles || !m_ThreadData)
	{
		return E_OUTOFMEMORY;
	}
	HANDLE sharedHandle = GetSharedHandle();
	// Create appropriate # of threads for duplication
	for (UINT i = 0; i < m_ThreadCount; i++)
	{
		std::wstring outputName = CreatedOutputs.at(i);
		m_ThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_ThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_ThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_ThreadData[i].Output = outputName;
		m_ThreadData[i].TexSharedHandle = sharedHandle;
		m_ThreadData[i].OffsetX = m_OutputRect.left;
		m_ThreadData[i].OffsetY = m_OutputRect.top;
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
		ERROR("Could not terminate Desktop Duplication capture thread");
		return E_FAIL;
	}
	WaitForThreadTermination();
	return S_OK;
}

//
// Get DX_RESOURCES
//
HRESULT duplication_capture::InitializeDx(_Out_ DX_RESOURCES* Data)
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
			D3D11_SDK_VERSION, &Data->Device, &FeatureLevel, &Data->Context);
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
	hr = Data->Device->CreateVertexShader(g_VS, Size, nullptr, &Data->VertexShader);
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
	hr = Data->Device->CreateInputLayout(Layout, NumElements, g_VS, Size, &Data->InputLayout);
	if (FAILED(hr))
	{
		return hr;
	}
	Data->Context->IASetInputLayout(Data->InputLayout);

	// Pixel shader
	Size = ARRAYSIZE(g_PS);
	hr = Data->Device->CreatePixelShader(g_PS, Size, nullptr, &Data->PixelShader);
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
	hr = Data->Device->CreateSamplerState(&SampDesc, &Data->SamplerLinear);
	if (FAILED(hr))
	{
		return hr;
	}

	return hr;
}

//
// Create shared texture
//
HRESULT duplication_capture::CreateSharedSurf(std::vector<std::wstring> outputs, _Out_ std::vector<std::wstring> *pCreatedOutputs, _Out_ RECT* pDeskBounds)
{
	HRESULT hr;

	// Get DXGI resources
	IDXGIDevice* DxgiDevice = nullptr;
	hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
	if (FAILED(hr))
	{
		ERROR(L"Failed to QI for DXGI Device");
		return hr;
	}

	IDXGIAdapter* DxgiAdapter = nullptr;
	hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
	DxgiDevice->Release();
	DxgiDevice = nullptr;
	if (FAILED(hr))
	{
		ERROR(L"Failed to get parent DXGI Adapter");
		return hr;
	}

	// Set initial values so that we always catch the right coordinates
	pDeskBounds->left = INT_MAX;
	pDeskBounds->right = INT_MIN;
	pDeskBounds->top = INT_MAX;
	pDeskBounds->bottom = INT_MIN;

	IDXGIOutput* DxgiOutput = nullptr;

	// Figure out right dimensions for full size desktop texture and # of outputs to duplicate
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
		ERROR(L"Failed to create shared texture");
		return hr;
	}
	// Get keyed mutex
	hr = m_SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&m_KeyMutex));
	if (FAILED(hr))
	{
		ERROR(L"Failed to query for keyed mutex in OUTPUTMANAGER");
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

HRESULT duplication_capture::AcquireNextFrame(ID3D11Texture2D **ppDesktopFrame, DWORD timeoutMillis)
{
	*ppDesktopFrame = nullptr;
	bool haveNewFrameData = false;
	for (UINT i = 0; i < m_ThreadCount; ++i)
	{
		if (m_ThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			haveNewFrameData = true;
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
	m_DeviceContext->CopyResource(pDesktopFrame, m_SharedSurf);

	QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);


	*ppDesktopFrame = pDesktopFrame;
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
	duplication_manager pDuplicationManager;
	mouse_pointer pMousePointer;

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

	{
		// Obtain handle to sync shared Surface
		hr = TData->DxRes.Device->OpenSharedResource(TData->TexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&SharedSurf));
		if (FAILED(hr))
		{
			ERROR(L"Opening shared texture failed");
			goto Exit;
		}
		ReleaseOnExit releaseSharedSurf(SharedSurf);
		hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&KeyMutex));
		if (FAILED(hr))
		{
			ERROR(L"Failed to get keyed mutex interface in spawned thread");
			goto Exit;
		}
		ReleaseOnExit releaseMutex(KeyMutex);
		// Make duplication manager
		hr = pDuplicationManager.Initialize(&TData->DxRes, TData->Output);

		if (FAILED(hr))
		{
			goto Exit;
		}

		// Get output description
		DXGI_OUTPUT_DESC DesktopDesc;
		RtlZeroMemory(&DesktopDesc, sizeof(DXGI_OUTPUT_DESC));
		pDuplicationManager.GetOutputDesc(&DesktopDesc);

		// Main duplication loop
		bool WaitToProcessCurrentFrame = false;
		FRAME_DATA CurrentData{};

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
			hr = KeyMutex->AcquireSync(0, 1000);
			if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
			{
				// Can't use shared surface right now, try again later
				WaitToProcessCurrentFrame = true;
				continue;
			}
			else if (FAILED(hr))
			{
				// Generic unknown failure
				ERROR(L"Unexpected error acquiring KeyMutex");
				pDuplicationManager.DoneWithFrame();
				break;
			}

			ReleaseKeyedMutexOnExit releaseMutex(KeyMutex, 1);

			// We can now process the current frame
			WaitToProcessCurrentFrame = false;

			// Get mouse info
			hr = pMousePointer.GetMouse(TData->PtrInfo, &(CurrentData.FrameInfo), DesktopDesc.DesktopCoordinates, pDuplicationManager.GetOutputDuplication());
			if (FAILED(hr))
			{
				pDuplicationManager.DoneWithFrame();
				KeyMutex->ReleaseSync(1);
				break;
			}

			// Process new frame
			hr = pDuplicationManager.ProcessFrame(&CurrentData, SharedSurf, TData->OffsetX, TData->OffsetY, &DesktopDesc);
			if (FAILED(hr))
			{
				pDuplicationManager.DoneWithFrame();
				KeyMutex->ReleaseSync(1);
				break;
			}

			// Release acquired keyed mutex
			hr = KeyMutex->ReleaseSync(1);
			if (FAILED(hr))
			{
				ERROR(L"Unexpected error releasing the keyed mutex");
				pDuplicationManager.DoneWithFrame();
				break;
			}

			// Release frame back to desktop duplication
			hr = pDuplicationManager.DoneWithFrame();
			if (FAILED(hr))
			{
				break;
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
			ERROR(L"Display device unavailable: %s", err.ErrorMessage());
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
			WARN(L"Desktop duplication temporarily unavailable: hr = 0x%08x, error = %s", hr, err.ErrorMessage());
			break;
		case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
			ERROR(L"Error reinitializing desktop duplication with DXGI_ERROR_NOT_CURRENTLY_AVAILABLE. This means DXGI reached the limit on the maximum number of concurrent duplication applications (default of four). Therefore, the calling application cannot create any desktop duplication interfaces until the other applications close");
			isUnexpectedError = true;
			break;
		default:
			//Unexpected error, return.
			ERROR(L"Error reinitializing desktop duplication with unexpected error, aborting: %s", err.ErrorMessage());
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

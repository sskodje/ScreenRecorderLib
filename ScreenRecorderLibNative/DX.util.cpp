#pragma warning (disable : 26451)
#include "DX.util.h"
#include "cleanup.h"

//
// Get DX_RESOURCES
//
HRESULT InitializeDx(_Out_ DX_RESOURCES* Data)
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
	return hr;
}

HRESULT GetOutputDescsForDeviceNames(_In_ std::vector<std::wstring> deviceNames, _Out_ std::vector<DXGI_OUTPUT_DESC> *outputDescs) {
	*outputDescs = std::vector<DXGI_OUTPUT_DESC>();

	// Figure out right dimensions for full size desktop texture and # of outputs to duplicate
	std::vector<IDXGIOutput*> outputs{};
	EnumOutputs(&outputs);
	for each (IDXGIOutput * output in outputs)
	{
		DXGI_OUTPUT_DESC desc;
		output->GetDesc(&desc);
		if (deviceNames.size() == 0 || std::find(deviceNames.begin(), deviceNames.end(), desc.DeviceName) != deviceNames.end())
		{
			outputDescs->push_back(desc);
		}
		output->Release();
	}
	return S_OK;
}

HRESULT GetMainOutput(_Outptr_result_maybenull_ IDXGIOutput **ppOutput) {
	HRESULT hr = S_FALSE;
	if (ppOutput) {
		*ppOutput = nullptr;
	}

	std::vector<IDXGIAdapter*> adapters = EnumDisplayAdapters();
	for (IDXGIAdapter *adapter : adapters)
	{
		IDXGIOutput *pOutput;
		int i = 0;
		while (adapter->EnumOutputs(i, &pOutput) != DXGI_ERROR_NOT_FOUND)
		{
			DXGI_OUTPUT_DESC desc;
			RETURN_ON_BAD_HR(pOutput->GetDesc(&desc));

			if (desc.DesktopCoordinates.left == 0 && desc.DesktopCoordinates.top == 0) {
				// Return the pointer to the caller.
				hr = S_OK;
				if (ppOutput) {
					*ppOutput = pOutput;
					(*ppOutput)->AddRef();
				}
				break;
			}
			SafeRelease(&pOutput);
			i++;
		}
		if (ppOutput && *ppOutput) {
			break;
		}
	}
	for (IDXGIAdapter *adapter : adapters)
	{
		SafeRelease(&adapter);
	}

	return hr;
}

HRESULT GetOutputForDeviceName(_In_ std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIOutput **ppOutput) {
	HRESULT hr = S_FALSE;
	if (ppOutput) {
		*ppOutput = nullptr;
	}
	if (deviceName != L"") {
		std::vector<IDXGIAdapter*> adapters = EnumDisplayAdapters();
		for (IDXGIAdapter *adapter : adapters)
		{
			IDXGIOutput *pOutput;
			int i = 0;
			while (adapter->EnumOutputs(i, &pOutput) != DXGI_ERROR_NOT_FOUND)
			{
				DXGI_OUTPUT_DESC desc;
				RETURN_ON_BAD_HR(pOutput->GetDesc(&desc));

				if (desc.DeviceName == deviceName) {
					// Return the pointer to the caller.
					hr = S_OK;
					if (ppOutput) {
						*ppOutput = pOutput;
						(*ppOutput)->AddRef();
					}
					break;
				}
				SafeRelease(&pOutput);
				i++;
			}
			if (ppOutput && *ppOutput) {
				break;
			}
		}
		for (IDXGIAdapter *adapter : adapters)
		{
			SafeRelease(&adapter);
		}
	}
	return hr;
}

void EnumOutputs(_Out_ std::vector<IDXGIOutput*> *pOutputs)
{
	*pOutputs = std::vector<IDXGIOutput*>();
	std::vector<IDXGIAdapter*> adapters = EnumDisplayAdapters();
	for (IDXGIAdapter *adapter : adapters)
	{
		IDXGIOutput *pOutput;
		int i = 0;
		while (adapter->EnumOutputs(i, &pOutput) != DXGI_ERROR_NOT_FOUND)
		{
			// Return the pointer to the caller.
			pOutputs->push_back(pOutput);
			i++;
		}
		SafeRelease(&adapter);
	}
}

void GetCombinedRects(_In_ std::vector<RECT> inputs, _Out_ RECT *pOutRect, _Out_opt_ std::vector<SIZE> *pOffsets)
{
	*pOutRect = RECT{};
	std::sort(inputs.begin(), inputs.end(), compareRects);
	if (pOffsets) {
		*pOffsets = std::vector<SIZE>();
	}
	for (int i = 0; i < inputs.size(); i++) {
		RECT curDisplay = inputs[i];
		UnionRect(pOutRect, &curDisplay, pOutRect);
		//The offset is the difference between the end of the previous screen and the start of the current screen.
		int xPosOffset = 0;
		int yPosOffset = 0;
		if (i > 0) {
			RECT prevDisplay = inputs[i - 1];
			xPosOffset = max(0, curDisplay.left - prevDisplay.right);
			yPosOffset = max(0, curDisplay.top - prevDisplay.bottom);
		}
		pOutRect->right -= xPosOffset;
		pOutRect->bottom -= yPosOffset;
		if (pOffsets) {
			pOffsets->push_back(SIZE{ xPosOffset,yPosOffset });
		}
	}
}

std::wstring GetMonitorName(HMONITOR monitor) {
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	GetMonitorInfoW(monitor, &info);

	UINT32 requiredPaths, requiredModes;
	GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes);
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(requiredModes);
	QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes.data(), nullptr);

	for (auto& p : paths) {
		DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
		sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		sourceName.header.size = sizeof(sourceName);
		sourceName.header.adapterId = p.sourceInfo.adapterId;
		sourceName.header.id = p.sourceInfo.id;
		DisplayConfigGetDeviceInfo(&sourceName.header);
		if (wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
			DISPLAYCONFIG_TARGET_DEVICE_NAME name;
			name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
			name.header.size = sizeof(name);
			name.header.adapterId = p.sourceInfo.adapterId;
			name.header.id = p.targetInfo.id;
			DisplayConfigGetDeviceInfo(&name.header);
			return std::wstring(name.monitorFriendlyDeviceName);
		}
	}
	return L"";
}

std::vector<IDXGIAdapter*> EnumDisplayAdapters()
{
	std::vector<IDXGIAdapter*> vAdapters;
	IDXGIFactory1 * pFactory;
	HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory), (void**)(&pFactory));
	if (SUCCEEDED(hr)) {
		UINT i = 0;
		IDXGIAdapter *pAdapter;
		while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
		{
			vAdapters.push_back(pAdapter);
			i++;
		}
	}
	SafeRelease(&pFactory);
	return vAdapters;
}

//
// Clean up DX_RESOURCES
//
void CleanDx(_Inout_ DX_RESOURCES* Data)
{
	SafeRelease(&Data->Device);
	SafeRelease(&Data->Context);
}
//
// Set new viewport
//
void SetViewPort(_In_ ID3D11DeviceContext * deviceContext, _In_ UINT Width, _In_ UINT Height)
{
	D3D11_VIEWPORT VP;
	VP.Width = static_cast<FLOAT>(Width);
	VP.Height = static_cast<FLOAT>(Height);
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	deviceContext->RSSetViewports(1, &VP);
}

void SetDebugName(_In_ ID3D11DeviceChild * child, _In_ const std::string & name)
{
#if _DEBUG
	if (child != nullptr)
		child->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.c_str());
#endif
}
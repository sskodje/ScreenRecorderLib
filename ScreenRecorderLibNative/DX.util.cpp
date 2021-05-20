#pragma warning (disable : 26451)
#include "DX.util.h"
#include "Cleanup.h"

//
// Get DX_RESOURCES
//
HRESULT InitializeDx(_In_opt_ IDXGIAdapter *pDxgiAdapter, _Out_ DX_RESOURCES *Data)
{
	HRESULT hr = S_OK;

	std::vector<D3D_DRIVER_TYPE> driverTypes;
	if (pDxgiAdapter) {
		driverTypes.push_back(D3D_DRIVER_TYPE_UNKNOWN);
	}
	else
	{
		driverTypes.push_back(D3D_DRIVER_TYPE_HARDWARE);
		driverTypes.push_back(D3D_DRIVER_TYPE_WARP);
		driverTypes.push_back(D3D_DRIVER_TYPE_REFERENCE);
	}
	UINT NumDriverTypes = driverTypes.size();

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
		hr = D3D11CreateDevice(
			pDxgiAdapter,
			driverTypes[DriverTypeIndex],
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			FeatureLevels,
			ARRAYSIZE(FeatureLevels),
			D3D11_SDK_VERSION,
			&Data->Device,
			&FeatureLevel,
			&Data->Context);
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
	std::vector<IDXGIOutput *> outputs{};
	EnumOutputs(&outputs);
	for each (IDXGIOutput * output in outputs)
	{
		DXGI_OUTPUT_DESC desc;
		output->GetDesc(&desc);
		if (std::find(deviceNames.begin(), deviceNames.end(), desc.DeviceName) != deviceNames.end()
			|| std::find(deviceNames.begin(), deviceNames.end(), ALL_MONITORS_ID) != deviceNames.end())
		{
			outputDescs->push_back(desc);
		}
		output->Release();
	}
	return S_OK;
}

HRESULT GetOutputRectsForRecordingSources(_In_ std::vector<RECORDING_SOURCE> sources, _Out_ std::vector<std::pair<RECORDING_SOURCE, RECT>> *outputs)
{
	std::vector<std::pair<RECORDING_SOURCE, RECT>> validOutputs{};
	int xOffset = 0;
	for each (RECORDING_SOURCE source in sources)
	{
		switch (source.Type)
		{
		case RecordingSourceType::Display: {
			if (source.CaptureDevice == ALL_MONITORS_ID) {
				std::vector<IDXGIOutput *> outputs;
				EnumOutputs(&outputs);
				for each (IDXGIOutput * output in outputs)
				{
					DXGI_OUTPUT_DESC outputDesc;
					output->GetDesc(&outputDesc);
					outputDesc.DesktopCoordinates.left += xOffset;
					outputDesc.DesktopCoordinates.right += xOffset;
					std::pair<RECORDING_SOURCE, RECT> tuple(source, outputDesc.DesktopCoordinates);
					validOutputs.push_back(tuple);
					output->Release();
				}
			}
			else {
				// Figure out right dimensions for full size desktop texture and # of outputs to duplicate
				DXGI_OUTPUT_DESC outputDesc;
				CComPtr<IDXGIOutput> output;
				HRESULT hr = GetOutputForDeviceName(source.CaptureDevice, &output);
				if (FAILED(hr))
				{
					LOG_ERROR(L"Failed to get output descs for selected devices");
					return hr;
				}
				RETURN_ON_BAD_HR(hr = output->GetDesc(&outputDesc));
				outputDesc.DesktopCoordinates.left += xOffset;
				outputDesc.DesktopCoordinates.right += xOffset;
				std::pair<RECORDING_SOURCE, RECT> tuple(source, outputDesc.DesktopCoordinates);
				validOutputs.push_back(tuple);
			}
			break;
		}
		case RecordingSourceType::Window: {
			RECT rect;
			if (GetWindowRect(source.WindowHandle, &rect))
			{
				rect = RECT{ 0,0, RectWidth(rect),RectHeight(rect) };
				LONG width = RectWidth(rect);
				if (validOutputs.size() > 0) {
					rect.right = validOutputs.back().second.right + width;
					rect.left = rect.right - width;
				}
				xOffset += width;
				std::pair<RECORDING_SOURCE, RECT> tuple(source, rect);
				validOutputs.push_back(tuple);
			}
			break;
		}
		default:
			break;
		}
	}
	auto sortRect = [](const std::pair<RECORDING_SOURCE, RECT> &p1, const std::pair<RECORDING_SOURCE, RECT> &p2)
	{
		RECT r1 = p1.second;
		RECT r2 = p2.second;
		return std::tie(r1.left, r1.top) < std::tie(r2.left, r2.top);
	};
	std::sort(validOutputs.begin(), validOutputs.end(), sortRect);
	*outputs = validOutputs;
	return S_OK;
}

HRESULT GetMainOutput(_Outptr_result_maybenull_ IDXGIOutput **ppOutput) {
	HRESULT hr = S_FALSE;
	if (ppOutput) {
		*ppOutput = nullptr;
	}

	std::vector<IDXGIAdapter *> adapters = EnumDisplayAdapters();
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
	HRESULT hr = E_FAIL;
	if (ppOutput) {
		*ppOutput = nullptr;
	}
	if (deviceName != L"") {
		std::vector<IDXGIAdapter *> adapters = EnumDisplayAdapters();
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

void EnumOutputs(_Out_ std::vector<IDXGIOutput *> *pOutputs)
{
	*pOutputs = std::vector<IDXGIOutput *>();
	std::vector<IDXGIAdapter *> adapters = EnumDisplayAdapters();
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
	auto sortRects = [](const RECT &r1, const RECT &r2)
	{
		return std::tie(r1.left, r1.top) < std::tie(r2.left, r2.top);
	};
	std::sort(inputs.begin(), inputs.end(), sortRects);
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
			//Fix any gaps in the output RECTS with offsets
			RECT prevDisplay = inputs[i - 1];
			if (curDisplay.left > prevDisplay.right) {
				xPosOffset = curDisplay.left - prevDisplay.right;
			}

			if (curDisplay.top > prevDisplay.bottom) {
				yPosOffset = curDisplay.top - prevDisplay.bottom;
			}
			pOutRect->right -= xPosOffset;
			pOutRect->bottom -= yPosOffset;
		}
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

	for (auto &p : paths) {
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

std::vector<IDXGIAdapter *> EnumDisplayAdapters()
{
	std::vector<IDXGIAdapter *> vAdapters;
	IDXGIFactory1 *pFactory;
	HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory), (void **)(&pFactory));
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
void CleanDx(_Inout_ DX_RESOURCES *Data)
{
	SafeRelease(&Data->Device);
	SafeRelease(&Data->Context);
}
//
// Set new viewport
//
void SetViewPort(_In_ ID3D11DeviceContext *deviceContext, _In_ UINT Width, _In_ UINT Height)
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

void SetDebugName(_In_ ID3D11DeviceChild *child, _In_ const std::string &name)
{
#if _DEBUG
	if (child != nullptr)
		child->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.c_str());
#endif
}
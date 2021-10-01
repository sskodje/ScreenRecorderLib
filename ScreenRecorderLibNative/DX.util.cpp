#pragma warning (disable : 26451)
#include "DX.util.h"
#include "Cleanup.h"
#include <dwmapi.h>
#include "VideoReader.h"
#include "CameraCapture.h"
#include "ImageReader.h"
#include "GifReader.h"
#include "WindowsGraphicsCapture.h"
using namespace DirectX;
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
	size_t NumDriverTypes = driverTypes.size();

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
#if _DEBUG 
			D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
#else
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
#endif
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
	CComPtr<ID3D10Multithread> pMulti = nullptr;
	RETURN_ON_BAD_HR(hr = Data->Context->QueryInterface(IID_PPV_ARGS(&pMulti)));
	pMulti->SetMultithreadProtected(TRUE);
	pMulti.Release();

#if _DEBUG 
	RETURN_ON_BAD_HR(hr = Data->Device->QueryInterface(IID_PPV_ARGS(&Data->Debug)));
#endif
	return hr;
}

HRESULT GetAdapterForDevice(_In_ ID3D11Device *pDevice, _Outptr_ IDXGIAdapter **ppAdapter)
{
	CComPtr<IDXGIDevice1> dxgiDevice = nullptr;
	HRESULT hr = pDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgiDevice));
	CComPtr<IDXGIAdapter> pDxgiAdapter;
	hr = dxgiDevice->GetParent(
	__uuidof(IDXGIAdapter),
	reinterpret_cast<void **>(&pDxgiAdapter));
	if (ppAdapter) {
		*ppAdapter = pDxgiAdapter;
		(*ppAdapter)->AddRef();
	}
	return hr;
}


HRESULT GetOutputRectsForRecordingSources(_In_ const std::vector<RECORDING_SOURCE*> &sources, _Out_ std::vector<std::pair<RECORDING_SOURCE*, RECT>> *outputs)
{
	std::vector<std::pair<RECORDING_SOURCE*, RECT>> validOutputs{};

	auto GetOffsetSourceRect([&](const RECT &originalSourceRect, RECORDING_SOURCE *source) {

		RECT offsetSourceRect = originalSourceRect;
		if (source->Position.has_value()) {
			OffsetRect(&offsetSourceRect, source->Position.value().x - offsetSourceRect.left, source->Position.value().y - offsetSourceRect.top);
		}
		else if (validOutputs.size() == 0) {
			//For the first source, we start at [0,0]
			OffsetRect(&offsetSourceRect, -offsetSourceRect.left, -offsetSourceRect.top);
		}
		else if (validOutputs.size() > 0) {
			for each (std::pair<RECORDING_SOURCE*, RECT> var in validOutputs)
			{
				RECT prevRect = var.second;
				RECT intersect{};
				if (IntersectRect(&intersect, &offsetSourceRect, &prevRect)) {
					OffsetRect(&offsetSourceRect, prevRect.right - intersect.left, 0);
				}
				if (IntersectRect(&intersect, &offsetSourceRect, &prevRect)) {
					OffsetRect(&offsetSourceRect, 0, prevRect.bottom - intersect.top);
				}
			}
		}
		if (source->OutputSize.has_value() && (source->OutputSize.value().cx > 0 || source->OutputSize.value().cy > 0)) {
			long cx = source->OutputSize.value().cx;
			long cy = source->OutputSize.value().cy;
			if (cx > 0 && cy == 0) {
				cy = static_cast<long>(round((static_cast<double>(RectHeight(originalSourceRect)) / static_cast<double>(RectWidth(originalSourceRect))) * cx));
			}
			else if (cx == 0 && cy > 0) {
				cx = static_cast<long>(round((static_cast<double>(RectWidth(originalSourceRect)) / static_cast<double>(RectHeight(originalSourceRect))) * cy));
			}
			offsetSourceRect.right = offsetSourceRect.left + cx;
			offsetSourceRect.bottom = offsetSourceRect.top + cy;
		}
		else if (IsValidRect(source->SourceRect.value_or(RECT{}))) {
			offsetSourceRect = source->SourceRect.value();
		}
		return offsetSourceRect;
		});

	for each (RECORDING_SOURCE *source in sources)
	{
		switch (source->Type)
		{
			case RecordingSourceType::Display: {
				CComPtr<IDXGIOutput> output;
				HRESULT hr = GetOutputForDeviceName(source->SourcePath, &output);
				if (FAILED(hr))
				{
					LOG_ERROR(L"Failed to get output descs for selected devices");
					return hr;
				}
				DXGI_OUTPUT_DESC outputDesc;
				output->GetDesc(&outputDesc);
				RECT displayRect = outputDesc.DesktopCoordinates;
				RECT sourceRect = GetOffsetSourceRect(displayRect, source);
				std::pair<RECORDING_SOURCE*, RECT> tuple(source, sourceRect);
				validOutputs.push_back(tuple);
				break;
			}
			case RecordingSourceType::Window: {
				WindowsGraphicsCapture capture;
				RECT windowRect{};
				SIZE windowSize;
				if (SUCCEEDED(capture.GetNativeSize(*source, &windowSize))) {
					windowRect.right = windowSize.cx;
					windowRect.bottom = windowSize.cy;

					RECT sourceRect = GetOffsetSourceRect(windowRect, source);
					std::pair<RECORDING_SOURCE *, RECT> tuple(source, sourceRect);
					validOutputs.push_back(tuple);
				}
				break;
			}
			case RecordingSourceType::Video: {
				SIZE size{};
				VideoReader reader{};
				HRESULT hr = reader.GetNativeSize(*source, &size);
				if (SUCCEEDED(hr)) {
					RECT sourceRect = GetOffsetSourceRect(RECT{ 0,0,size.cx,size.cx }, source);
					LONG width = RectWidth(sourceRect);
					std::pair<RECORDING_SOURCE *, RECT> tuple(source, sourceRect);
					validOutputs.push_back(tuple);
				}
				break;
			}
			case RecordingSourceType::CameraCapture: {
				SIZE size{};
				CameraCapture reader{};
				HRESULT hr = reader.GetNativeSize(*source, &size);
				if (SUCCEEDED(hr)) {
					RECT sourceRect = GetOffsetSourceRect(RECT{ 0,0,size.cx,size.cy }, source);
					LONG width = RectWidth(sourceRect);
					std::pair<RECORDING_SOURCE *, RECT> tuple(source, sourceRect);
					validOutputs.push_back(tuple);
				}
				break;
			}
			case RecordingSourceType::Picture: {
				SIZE size{};
				std::string signature = ReadFileSignature(source->SourcePath.c_str());
				ImageFileType imageType = getImageTypeByMagic(signature.c_str());
				std::unique_ptr<CaptureBase> reader = nullptr;
				if (imageType == ImageFileType::IMAGE_FILE_GIF) {
					reader = std::make_unique<GifReader>();
				}
				else {
					reader = std::make_unique<ImageReader>();
				}
				HRESULT hr = reader->GetNativeSize(*source, &size);
				if (SUCCEEDED(hr)) {
					RECT sourceRect = GetOffsetSourceRect(RECT{ 0,0,size.cx,size.cy }, source);
					LONG width = RectWidth(sourceRect);
					std::pair<RECORDING_SOURCE*, RECT> tuple(source, sourceRect);
					validOutputs.push_back(tuple);
				}
				break;
			}
			default:
				break;
		}
	}
	auto sortRect = [](const std::pair<RECORDING_SOURCE*, RECT> &p1, const std::pair<RECORDING_SOURCE*, RECT> &p2)
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

HRESULT GetAdapterForDeviceName(_In_ std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIAdapter **ppAdapter) {
	CComPtr<IDXGIOutput> pSelectedOutput = nullptr;
	CComPtr<IDXGIAdapter> pDxgiAdapter = nullptr;
	HRESULT hr = GetOutputForDeviceName(deviceName, &pSelectedOutput);
	if (SUCCEEDED(hr)) {
		// Get DXGI adapter
		hr = pSelectedOutput->GetParent(
			__uuidof(IDXGIAdapter),
			reinterpret_cast<void **>(&pDxgiAdapter));
	}
	if (ppAdapter) {
		*ppAdapter = pDxgiAdapter;
		(*ppAdapter)->AddRef();
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
		RECT curRecordingSource = inputs[i];
		UnionRect(pOutRect, &curRecordingSource, pOutRect);
		//The offset is the difference between the end of the previous source and the start of the current source.
		int xPosOffset = 0;
		int yPosOffset = 0;

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
	HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)(&pFactory));
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
	SafeRelease(&Data->Debug);
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


//
// Initialize shaders for drawing to screen
//
HRESULT InitShaders(_In_ ID3D11Device *pDevice, _Outptr_ ID3D11PixelShader **ppPixelShader, _Outptr_ ID3D11VertexShader **ppVertexShader, _Outptr_ ID3D11InputLayout **ppInputLayout)
{
	HRESULT hr;
	CComPtr<ID3D11DeviceContext> pDeviceContext;
	pDevice->GetImmediateContext(&pDeviceContext);
	UINT Size = ARRAYSIZE(g_VS);
	CComPtr<ID3D11VertexShader> vertexShader;
	hr = pDevice->CreateVertexShader(g_VS, Size, nullptr, &vertexShader);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create vertex shader: %lls", err.ErrorMessage());
		return hr;
	}
	D3D11_INPUT_ELEMENT_DESC Layout[] =
	{
		{ "SV_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	UINT NumElements = ARRAYSIZE(Layout);
	CComPtr<ID3D11InputLayout> inputLayout;
	hr = pDevice->CreateInputLayout(Layout, NumElements, g_VS, Size, &inputLayout);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create input layout: %lls", err.ErrorMessage());
		return hr;
	}
	pDeviceContext->IASetInputLayout(inputLayout);

	Size = ARRAYSIZE(g_PS);
	CComPtr<ID3D11PixelShader> pixelShader;
	hr = pDevice->CreatePixelShader(g_PS, Size, nullptr, &pixelShader);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create pixel shader: %lls", err.ErrorMessage());
		return hr;
	}

	*ppPixelShader = pixelShader;
	(*ppPixelShader)->AddRef();


	*ppVertexShader = vertexShader;
	(*ppVertexShader)->AddRef();


	*ppInputLayout = inputLayout;
	(*ppInputLayout)->AddRef();

	return hr;
}

//
// Returns shared handle
//
_Ret_maybenull_ HANDLE GetSharedHandle(_In_ ID3D11Texture2D *pSurface)
{
	if (!pSurface) {
		return nullptr;
	}
	HANDLE Hnd = nullptr;

	// QI IDXGIResource interface to synchronized shared surface.
	IDXGIResource *DXGIResource = nullptr;
	HRESULT hr = pSurface->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&DXGIResource));
	if (SUCCEEDED(hr))
	{
		// Obtain handle to IDXGIResource object.
		DXGIResource->GetSharedHandle(&Hnd);
		DXGIResource->Release();
		DXGIResource = nullptr;
	}

	return Hnd;
}
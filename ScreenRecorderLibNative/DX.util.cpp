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
			SafeRelease(&adapter);
			if (ppOutput && *ppOutput) {
				break;
			}
		}
	}
	return hr;
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
		child->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());
#endif
}
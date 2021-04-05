#pragma once
#include "screengrab.h"
#include <Windows.h>
#include "log.h"
#include <atlbase.h>
#include "cleanup.h"
namespace {
	bool g_WIC2 = false;

	bool _IsWIC2()
	{
		return g_WIC2;
	}

	IWICImagingFactory* _GetWIC()
	{
		static INIT_ONCE s_initOnce = INIT_ONCE_STATIC_INIT;

		IWICImagingFactory* factory = nullptr;
		InitOnceExecuteOnce(&s_initOnce,
			[](PINIT_ONCE, PVOID, PVOID *factory) -> BOOL
			{
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8) || defined(_WIN7_PLATFORM_UPDATE)
				HRESULT hr = CoCreateInstance(
					CLSID_WICImagingFactory2,
					nullptr,
					CLSCTX_INPROC_SERVER,
					__uuidof(IWICImagingFactory2),
					factory
				);

				if (SUCCEEDED(hr))
				{
					// WIC2 is available on Windows 10, Windows 8.x, and Windows 7 SP1 with KB 2670838 installed
					g_WIC2 = true;
					return TRUE;
				}
				else
				{
					hr = CoCreateInstance(
						CLSID_WICImagingFactory1,
						nullptr,
						CLSCTX_INPROC_SERVER,
						__uuidof(IWICImagingFactory),
						factory
					);
					return SUCCEEDED(hr) ? TRUE : FALSE;
				}
#else
				return SUCCEEDED(CoCreateInstance(
					CLSID_WICImagingFactory,
					nullptr,
					CLSCTX_INPROC_SERVER,
					__uuidof(IWICImagingFactory),
					factory)) ? TRUE : FALSE;
#endif
			}, nullptr, reinterpret_cast<LPVOID*>(&factory));

		return factory;
	}


	inline DXGI_FORMAT EnsureNotTypeless(DXGI_FORMAT fmt)
	{
		// Assumes UNORM or FLOAT; doesn't use UINT or SINT
		switch (fmt)
		{
		case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case DXGI_FORMAT_R32G32B32_TYPELESS:    return DXGI_FORMAT_R32G32B32_FLOAT;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_UNORM;
		case DXGI_FORMAT_R32G32_TYPELESS:       return DXGI_FORMAT_R32G32_FLOAT;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return DXGI_FORMAT_R10G10B10A2_UNORM;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_R16G16_TYPELESS:       return DXGI_FORMAT_R16G16_UNORM;
		case DXGI_FORMAT_R32_TYPELESS:          return DXGI_FORMAT_R32_FLOAT;
		case DXGI_FORMAT_R8G8_TYPELESS:         return DXGI_FORMAT_R8G8_UNORM;
		case DXGI_FORMAT_R16_TYPELESS:          return DXGI_FORMAT_R16_UNORM;
		case DXGI_FORMAT_R8_TYPELESS:           return DXGI_FORMAT_R8_UNORM;
		case DXGI_FORMAT_BC1_TYPELESS:          return DXGI_FORMAT_BC1_UNORM;
		case DXGI_FORMAT_BC2_TYPELESS:          return DXGI_FORMAT_BC2_UNORM;
		case DXGI_FORMAT_BC3_TYPELESS:          return DXGI_FORMAT_BC3_UNORM;
		case DXGI_FORMAT_BC4_TYPELESS:          return DXGI_FORMAT_BC4_UNORM;
		case DXGI_FORMAT_BC5_TYPELESS:          return DXGI_FORMAT_BC5_UNORM;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:     return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:     return DXGI_FORMAT_B8G8R8X8_UNORM;
		case DXGI_FORMAT_BC7_TYPELESS:          return DXGI_FORMAT_BC7_UNORM;
		default:                                return fmt;
		}
	}

	//--------------------------------------------------------------------------------------
	HRESULT CaptureTexture(_In_ ID3D11DeviceContext* pContext,
		_In_ ID3D11Resource* pSource,
		D3D11_TEXTURE2D_DESC& desc,
		CComPtr<ID3D11Texture2D>& pStaging)
	{
		if (!pContext || !pSource)
			return E_INVALIDARG;

		D3D11_RESOURCE_DIMENSION resType = D3D11_RESOURCE_DIMENSION_UNKNOWN;
		pSource->GetType(&resType);

		if (resType != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
			return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

		CComPtr<ID3D11Texture2D> pTexture;
		HRESULT hr = pSource->QueryInterface(IID_PPV_ARGS(&pTexture));
		if (FAILED(hr))
			return hr;

		assert(pTexture);

		pTexture->GetDesc(&desc);

		CComPtr<ID3D11Device> d3dDevice;
		pContext->GetDevice(&d3dDevice);

		if (desc.SampleDesc.Count > 1)
		{
			// MSAA content must be resolved before being copied to a staging texture
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;

			CComPtr<ID3D11Texture2D> pTemp;
			hr = d3dDevice->CreateTexture2D(&desc, 0, &pTemp);
			if (FAILED(hr))
				return hr;

			assert(pTemp);

			DXGI_FORMAT fmt = EnsureNotTypeless(desc.Format);

			UINT support = 0;
			hr = d3dDevice->CheckFormatSupport(fmt, &support);
			if (FAILED(hr))
				return hr;

			if (!(support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE))
				return E_FAIL;

			for (UINT item = 0; item < desc.ArraySize; ++item)
			{
				for (UINT level = 0; level < desc.MipLevels; ++level)
				{
					UINT index = D3D11CalcSubresource(level, item, desc.MipLevels);
					pContext->ResolveSubresource(pTemp, index, pSource, index, fmt);
				}
			}

			desc.BindFlags = 0;
			desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			desc.Usage = D3D11_USAGE_STAGING;

			hr = d3dDevice->CreateTexture2D(&desc, 0, &pStaging);
			if (FAILED(hr))
				return hr;

			assert(pStaging);

			pContext->CopyResource(pStaging, pTemp);
		}
		else if ((desc.Usage == D3D11_USAGE_STAGING) && (desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ))
		{
			// Handle case where the source is already a staging texture we can use directly
			pStaging = pTexture;
		}
		else
		{
			// Otherwise, create a staging texture from the non-MSAA source
			desc.BindFlags = 0;
			desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			desc.Usage = D3D11_USAGE_STAGING;

			hr = d3dDevice->CreateTexture2D(&desc, 0, &pStaging);
			if (FAILED(hr))
				return hr;

			assert(pStaging);

			pContext->CopyResource(pStaging, pSource);
		}
		return S_OK;
	}
} // anonymous namespace

HRESULT __cdecl SaveWICTextureToFile(
	_In_ ID3D11DeviceContext* pContext,
	_In_ ID3D11Resource* pSource,
	_In_ REFGUID guidContainerFormat,
	_In_z_ const wchar_t* filePath,
	_In_opt_ const GUID* targetFormat,
	_In_opt_ std::function<void(IPropertyBag2*)> setCustomProps)
{
	if (!filePath)
		return E_INVALIDARG;

	D3D11_TEXTURE2D_DESC desc = {};
	CComPtr<ID3D11Texture2D> pStaging;
	HRESULT hr = CaptureTexture(pContext, pSource, desc, pStaging);
	if (FAILED(hr))
		return hr;

	// Determine source format's WIC equivalent
	WICPixelFormatGUID pfGuid;
	bool sRGB = false;
	switch (desc.Format)
	{
	case DXGI_FORMAT_R32G32B32A32_FLOAT:            pfGuid = GUID_WICPixelFormat128bppRGBAFloat; break;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:            pfGuid = GUID_WICPixelFormat64bppRGBAHalf; break;
	case DXGI_FORMAT_R16G16B16A16_UNORM:            pfGuid = GUID_WICPixelFormat64bppRGBA; break;
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:    pfGuid = GUID_WICPixelFormat32bppRGBA1010102XR; break; // DXGI 1.1
	case DXGI_FORMAT_R10G10B10A2_UNORM:             pfGuid = GUID_WICPixelFormat32bppRGBA1010102; break;
	case DXGI_FORMAT_B5G5R5A1_UNORM:                pfGuid = GUID_WICPixelFormat16bppBGRA5551; break;
	case DXGI_FORMAT_B5G6R5_UNORM:                  pfGuid = GUID_WICPixelFormat16bppBGR565; break;
	case DXGI_FORMAT_R32_FLOAT:                     pfGuid = GUID_WICPixelFormat32bppGrayFloat; break;
	case DXGI_FORMAT_R16_FLOAT:                     pfGuid = GUID_WICPixelFormat16bppGrayHalf; break;
	case DXGI_FORMAT_R16_UNORM:                     pfGuid = GUID_WICPixelFormat16bppGray; break;
	case DXGI_FORMAT_R8_UNORM:                      pfGuid = GUID_WICPixelFormat8bppGray; break;
	case DXGI_FORMAT_A8_UNORM:                      pfGuid = GUID_WICPixelFormat8bppAlpha; break;

	case DXGI_FORMAT_R8G8B8A8_UNORM:
		pfGuid = GUID_WICPixelFormat32bppRGBA;
		break;

	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		pfGuid = GUID_WICPixelFormat32bppRGBA;
		sRGB = true;
		break;

	case DXGI_FORMAT_B8G8R8A8_UNORM: // DXGI 1.1
		pfGuid = GUID_WICPixelFormat32bppBGRA;
		break;

	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: // DXGI 1.1
		pfGuid = GUID_WICPixelFormat32bppBGRA;
		sRGB = true;
		break;

	case DXGI_FORMAT_B8G8R8X8_UNORM: // DXGI 1.1
		pfGuid = GUID_WICPixelFormat32bppBGR;
		break;

	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: // DXGI 1.1
		pfGuid = GUID_WICPixelFormat32bppBGR;
		sRGB = true;
		break;

	default:
		return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
	}

	CComPtr<IWICImagingFactory> pWIC = _GetWIC();
	if (!pWIC)
		return E_NOINTERFACE;
	CComPtr<IWICStream> stream;
	hr = pWIC->CreateStream(&stream);
	if (FAILED(hr))
		return hr;

	hr = stream->InitializeFromFilename(filePath, GENERIC_WRITE);
	if (FAILED(hr))
		return hr;

	DeleteFileOnExit delonfail(stream, filePath);

	CComPtr<IWICBitmapEncoder> encoder;
	hr = pWIC->CreateEncoder(guidContainerFormat, 0, &encoder);
	if (FAILED(hr))
		return hr;

	hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
	if (FAILED(hr))
		return hr;

	CComPtr<IWICBitmapFrameEncode> frame;
	CComPtr<IPropertyBag2> props;
	hr = encoder->CreateNewFrame(&frame, &props);
	if (FAILED(hr))
		return hr;

	if (targetFormat && memcmp(&guidContainerFormat, &GUID_ContainerFormatBmp, sizeof(WICPixelFormatGUID)) == 0 && _IsWIC2())
	{
		// Opt-in to the WIC2 support for writing 32-bit Windows BMP files with an alpha channel
		PROPBAG2 option = {};
		option.pstrName = const_cast<wchar_t*>(L"EnableV5Header32bppBGRA");

		VARIANT varValue;
		varValue.vt = VT_BOOL;
		varValue.boolVal = VARIANT_TRUE;
		(void)props->Write(1, &option, &varValue);
	}

	if (setCustomProps)
	{
		setCustomProps(props);
	}

	hr = frame->Initialize(props);
	if (FAILED(hr))
		return hr;

	hr = frame->SetSize(desc.Width, desc.Height);
	if (FAILED(hr))
		return hr;

	hr = frame->SetResolution(72, 72);
	if (FAILED(hr))
		return hr;

	// Pick a target format
	WICPixelFormatGUID targetGuid;
	if (targetFormat)
	{
		targetGuid = *targetFormat;
	}
	else
	{
		// Screenshots don’t typically include the alpha channel of the render target
		switch (desc.Format)
		{
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8) || defined(_WIN7_PLATFORM_UPDATE)
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			if (_IsWIC2())
			{
				targetGuid = GUID_WICPixelFormat96bppRGBFloat;
			}
			else
			{
				targetGuid = GUID_WICPixelFormat24bppBGR;
			}
			break;
#endif

		case DXGI_FORMAT_R16G16B16A16_UNORM: targetGuid = GUID_WICPixelFormat48bppBGR; break;
		case DXGI_FORMAT_B5G5R5A1_UNORM:     targetGuid = GUID_WICPixelFormat16bppBGR555; break;
		case DXGI_FORMAT_B5G6R5_UNORM:       targetGuid = GUID_WICPixelFormat16bppBGR565; break;

		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_A8_UNORM:
			targetGuid = GUID_WICPixelFormat8bppGray;
			break;

		default:
			targetGuid = GUID_WICPixelFormat24bppBGR;
			break;
		}
	}

	hr = frame->SetPixelFormat(&targetGuid);
	if (FAILED(hr))
		return hr;

	if (targetFormat && memcmp(targetFormat, &targetGuid, sizeof(WICPixelFormatGUID)) != 0)
	{
		// Requested output pixel format is not supported by the WIC codec
		return E_FAIL;
	}

	// Encode WIC metadata
	CComPtr<IWICMetadataQueryWriter> metawriter;
	if (SUCCEEDED(frame->GetMetadataQueryWriter(&metawriter)))
	{
		PROPVARIANT value;
		PropVariantInit(&value);

		value.vt = VT_LPSTR;
		value.pszVal = const_cast<char*>("DirectXTK");

		if (memcmp(&guidContainerFormat, &GUID_ContainerFormatPng, sizeof(GUID)) == 0)
		{
			// Set Software name
			(void)metawriter->SetMetadataByName(L"/tEXt/{str=Software}", &value);

			// Set sRGB chunk
			if (sRGB)
			{
				value.vt = VT_UI1;
				value.bVal = 0;
				(void)metawriter->SetMetadataByName(L"/sRGB/RenderingIntent", &value);
			}
		}
		else
		{
			// Set Software name
			(void)metawriter->SetMetadataByName(L"System.ApplicationName", &value);

			if (sRGB)
			{
				// Set EXIF Colorspace of sRGB
				value.vt = VT_UI2;
				value.uiVal = 1;
				(void)metawriter->SetMetadataByName(L"System.Image.ColorSpace", &value);
			}
		}
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	hr = pContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(hr))
		return hr;

	if (memcmp(&targetGuid, &pfGuid, sizeof(WICPixelFormatGUID)) != 0)
	{
		// Conversion required to write
		CComPtr<IWICBitmap> source;
		hr = pWIC->CreateBitmapFromMemory(desc.Width, desc.Height, pfGuid,
			mapped.RowPitch, mapped.RowPitch * desc.Height,
			reinterpret_cast<BYTE*>(mapped.pData), &source);
		if (FAILED(hr))
		{
			pContext->Unmap(pStaging, 0);
			return hr;
		}

		CComPtr<IWICFormatConverter> FC;
		hr = pWIC->CreateFormatConverter(&FC);
		if (FAILED(hr))
		{
			pContext->Unmap(pStaging, 0);
			return hr;
		}

		BOOL canConvert = FALSE;
		hr = FC->CanConvert(pfGuid, targetGuid, &canConvert);
		if (FAILED(hr) || !canConvert)
		{
			return E_UNEXPECTED;
		}

		hr = FC->Initialize(source, targetGuid, WICBitmapDitherTypeNone, 0, 0, WICBitmapPaletteTypeMedianCut);
		if (FAILED(hr))
		{
			pContext->Unmap(pStaging, 0);
			return hr;
		}

		WICRect rect = { 0, 0, static_cast<INT>(desc.Width), static_cast<INT>(desc.Height) };
		hr = frame->WriteSource(FC, &rect);
		if (FAILED(hr))
		{
			pContext->Unmap(pStaging, 0);
			return hr;
		}
	}
	else
	{
		// No conversion required
		hr = frame->WritePixels(desc.Height, mapped.RowPitch, mapped.RowPitch * desc.Height, reinterpret_cast<BYTE*>(mapped.pData));
		if (FAILED(hr))
			return hr;
	}

	pContext->Unmap(pStaging, 0);

	hr = frame->Commit();
	if (FAILED(hr))
		return hr;

	hr = encoder->Commit();
	if (FAILED(hr))
		return hr;

	delonfail.clear();

	return S_OK;
}

HRESULT CreateWICBitmapFromFile(_In_z_ const wchar_t* filePath, _In_ const GUID targetFormat, _Outptr_ IWICBitmapSource **ppIWICBitmapSource)
{
	HRESULT hr = S_OK;
	if (ppIWICBitmapSource) {
		*ppIWICBitmapSource = nullptr;
	}

	if (!filePath)
		return E_INVALIDARG;

	// Step 1: Decode the source image
	auto pWIC = _GetWIC();
	if (!pWIC)
		return E_NOINTERFACE;
	// Create a decoder
	CComPtr<IWICBitmapDecoder> pDecoder = NULL;
	RETURN_ON_BAD_HR(hr = pWIC->CreateDecoderFromFilename(
		filePath,                      // Image to be decoded
		NULL,                            // Do not prefer a particular vendor
		GENERIC_READ,                    // Desired read access to the file
		WICDecodeMetadataCacheOnDemand,  // Cache metadata when needed
		&pDecoder                        // Pointer to the decoder
	));

	CComPtr<IWICBitmapFrameDecode> pFrame = NULL;
	RETURN_ON_BAD_HR(hr = pDecoder->GetFrame(0, &pFrame));

	WICPixelFormatGUID sourcePixelFormat;
	RETURN_ON_BAD_HR(hr = pFrame->GetPixelFormat(&sourcePixelFormat));

	// Convert to 32bpp RGBA for easier processing.
	CComPtr<IWICBitmapSource> pConvertedFrame = NULL;
	RETURN_ON_BAD_HR(hr = WICConvertBitmapSource(targetFormat, pFrame, &pConvertedFrame));

	if (SUCCEEDED(hr))
	{
		if (ppIWICBitmapSource) {
			*ppIWICBitmapSource = pConvertedFrame;
			(*ppIWICBitmapSource)->AddRef();
		}
	}
	return hr;
}

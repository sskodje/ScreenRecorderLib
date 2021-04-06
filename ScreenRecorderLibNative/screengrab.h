#pragma once
#include <d3d11_1.h>
#include <functional>
#include <wincodec.h>

HRESULT __cdecl SaveWICTextureToFile(
	_In_ ID3D11DeviceContext *pContext,
	_In_ ID3D11Resource *pSource,
	_In_ REFGUID guidContainerFormat,
	_In_z_ const wchar_t *filePath,
	_In_opt_ const GUID *targetFormat = nullptr,
	_In_opt_ std::function<void __cdecl(IPropertyBag2 *)> setCustomProps = nullptr);

HRESULT CreateWICBitmapFromFile(
	_In_z_ const wchar_t *filePath,
	_In_ const GUID targetFormat,
	_Outptr_ IWICBitmapSource **ppIWICBitmapSource);
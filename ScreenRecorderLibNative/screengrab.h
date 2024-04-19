#pragma once
#include <d3d11_1.h>
#include <functional>
#include <wincodec.h>
#include <optional>

#pragma comment(lib, "Windowscodecs.lib")

HRESULT __cdecl SaveWICTextureToFile(
	_In_ ID3D11DeviceContext *pContext,
	_In_ ID3D11Resource *pSource,
	_In_ REFGUID guidContainerFormat,
	_In_z_ const wchar_t *filePath,
	_In_opt_ const std::optional<SIZE> destSize = std::nullopt,
	_In_opt_ const GUID *targetFormat = nullptr,
	_In_opt_ std::function<void __cdecl(IPropertyBag2 *)> setCustomProps = nullptr);

HRESULT __cdecl SaveWICTextureToStream(
	_In_ ID3D11DeviceContext *pContext,
	_In_ ID3D11Resource *pSource,
	_In_ REFGUID guidContainerFormat,
	_In_ IStream *pStream,
	_In_opt_ const std::optional<SIZE> destSize = std::nullopt,
	_In_opt_ const GUID *targetFormat = nullptr,
	_In_opt_ std::function<void __cdecl(IPropertyBag2 *)> setCustomProps = nullptr);

HRESULT __cdecl SaveWICTextureToWicStream(
	_In_ ID3D11DeviceContext *pContext,
	_In_ ID3D11Resource *pSource,
	_In_ REFGUID guidContainerFormat,
	_In_ IWICStream *pStream,
	_In_opt_ const std::optional<SIZE> destSize = std::nullopt,
	_In_opt_ const GUID *targetFormat = nullptr,
	_In_opt_ std::function<void __cdecl(IPropertyBag2 *)> setCustomProps = nullptr);

HRESULT CreateWICBitmapFromFile(
	_In_z_ const wchar_t *filePath,
	_In_ const GUID targetFormat,
	_Outptr_ IWICBitmapSource **ppIWICBitmapSource);

HRESULT CreateWICBitmapFromStream(
	_In_ IStream *pStream,
	_In_ const GUID targetFormat,
	_Outptr_ IWICBitmapSource **ppIWICBitmapSource);
#pragma once
#include <d3d11_1.h>
#include <functional>

HRESULT __cdecl SaveWICTextureToFile(
    _In_ ID3D11DeviceContext* pContext,
    _In_ ID3D11Resource* pSource,
    _In_ REFGUID guidContainerFormat,
    _In_z_ const wchar_t* fileName,
    _In_ UINT widthCrop = 0,
    _In_ UINT heightCrop = 0,
    _In_opt_ const GUID* targetFormat = nullptr,
    _In_opt_ std::function<void __cdecl(IPropertyBag2*)> setCustomProps = nullptr);
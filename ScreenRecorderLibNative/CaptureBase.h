#pragma once
#include "CommonTypes.h"
class CaptureBase abstract
{
public:
	virtual HRESULT StartCapture(_In_ std::wstring source) abstract;
	virtual HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_ ID3D11Texture2D **ppFrame) abstract;
	virtual HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice) abstract;
	virtual HRESULT GetNativeSize(_In_ std::wstring source, _Out_ SIZE *nativeMediaSize) abstract;
};
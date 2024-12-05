#pragma once
#include "CommonTypes.h"
#include "TextureManager.h"
class CaptureBase abstract
{
public:
	CaptureBase();
	virtual ~CaptureBase();
	virtual HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice) abstract;
	virtual HRESULT StartCapture(_In_ RECORDING_SOURCE_BASE &source) abstract;
	virtual HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame) abstract;
	virtual HRESULT WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ ID3D11Texture2D *pTexture = nullptr) abstract;
	virtual HRESULT GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *nativeMediaSize) abstract;
	virtual HRESULT GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ RECT frameCoordinates, _In_ int offsetX, _In_ int offsetY) abstract;
	virtual std::wstring Name() abstract;
	virtual HRESULT SendBitmapCallback(_In_ ID3D11Texture2D *pTexture);
	/// <summary>
	/// Calculate the offset used to position the content withing the parent frame based on the given anchor.
	/// </summary>
	/// <param name="anchor"></param>
	/// <param name="parentRect"></param>
	/// <param name="contentRect"></param>
	/// <returns></returns>
	virtual SIZE GetContentOffset(_In_ ContentAnchor anchor, _In_ RECT parentRect, _In_ RECT contentRect);
protected:

	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	std::unique_ptr<TextureManager> m_TextureManager;
	RECORDING_SOURCE_BASE *m_RecordingSource;
	LARGE_INTEGER m_LastGrabTimeStamp;

private:
	ID3D11Texture2D *m_FrameDataCallbackTexture;
	D3D11_TEXTURE2D_DESC m_FrameDataCallbackTextureDesc;
};
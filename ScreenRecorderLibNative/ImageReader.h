#pragma once
#include "CaptureBase.h"
#include "CommonTypes.h"
#include <memory>
#include "TextureManager.h"
#include <atlbase.h>

class ImageReader :public CaptureBase
{
public:
	ImageReader();
	~ImageReader();
	virtual HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice) override;
	virtual HRESULT StartCapture(_In_ RECORDING_SOURCE_BASE &source) override;
	virtual HRESULT GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *nativeMediaSize) override;
	virtual inline HRESULT StopCapture() { return S_OK; };
	virtual HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_result_maybenull_ ID3D11Texture2D **ppFrame) override;
	virtual HRESULT WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect) override;
	inline virtual HRESULT GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ RECT frameCoordinates, _In_ int offsetX, _In_ int offsetY) override {
		return S_FALSE;
	}
	virtual inline std::wstring Name() override { return L"ImageReader"; };

private:
	HRESULT InitializeDecoder(_In_ std::wstring source);
	HRESULT InitializeDecoder(_In_ IStream *pSourceStream);
	HRESULT InitializeDecoder(_In_ IWICBitmapSource *pBitmap);
	HRESULT SendBitmapCallback(_In_ ID3D11Texture2D *pSharedSurf);

	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	std::unique_ptr<TextureManager> m_TextureManager;
	CComPtr<ID3D11Texture2D> m_Texture;
	SIZE m_NativeSize;
	LARGE_INTEGER m_LastGrabTimeStamp;
	RECORDING_SOURCE_BASE *m_RecordingSource;
};
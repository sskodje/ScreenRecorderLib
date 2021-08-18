#pragma once
#include "CommonTypes.h"
#include "Log.h"
#include "DX.util.h"
#include "Util.h"
#include "Screengrab.h"
#include "TextureManager.h"
class ScreenCaptureBase abstract
{
public:
	ScreenCaptureBase();
	virtual ~ScreenCaptureBase();
	virtual HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice);
	virtual inline PTR_INFO *GetPointerInfo() {
		return &m_PtrInfo;
	}
	virtual RECT GetOutputRect() { return m_OutputRect; }
	virtual SIZE GetOutputSize() { return SIZE{ RectWidth(m_OutputRect),RectHeight(m_OutputRect) }; }
	virtual HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame);
	virtual HRESULT StartCapture(_In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
	virtual HRESULT StopCapture();
	virtual bool IsUpdatedFramesAvailable();
	virtual bool IsInitialFrameWriteComplete();
	virtual bool IsCapturing() { return m_IsCapturing; }
	virtual UINT GetUpdatedFrameCount(_In_ bool resetUpdatedFrameCounts);
	virtual bool IsSingleWindowCapture() final;
protected:
	LARGE_INTEGER m_LastAcquiredFrameTimeStamp;
	ID3D11Texture2D *m_SharedSurf;
	IDXGIKeyedMutex *m_KeyMutex;
	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	RECT m_OutputRect;
	PTR_INFO m_PtrInfo;

	virtual HRESULT CreateSharedSurf(_In_ RECT desktopRect, _Outptr_ ID3D11Texture2D **ppSharedTexture, _Outptr_ IDXGIKeyedMutex **ppKeyedMutex);
	virtual HRESULT CreateSharedSurf(_In_ std::vector<RECORDING_SOURCE> sources, _Out_ std::vector<RECORDING_SOURCE_DATA *> *pCreatedOutputs, _Out_ RECT *pDeskBounds);
	virtual LPTHREAD_START_ROUTINE GetCaptureThreadProc() = 0;
	_Ret_maybenull_  HANDLE GetSharedHandle(_In_ ID3D11Texture2D *pSurface);
	//HRESULT ProcessSources(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Out_ int *updateCount);
	//HRESULT ProcessOverlays(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Out_ int *updateCount);
private:
	bool m_IsCapturing;
	HANDLE m_TerminateThreadsEvent;

	std::unique_ptr<TextureManager> m_TextureManager;

	UINT m_OverlayThreadCount;
	_Field_size_(m_OverlayThreadCount) HANDLE *m_OverlayThreadHandles;
	_Field_size_(m_OverlayThreadCount) OVERLAY_THREAD_DATA *m_OverlayThreadData;

	UINT m_CaptureThreadCount;
	_Field_size_(m_CaptureThreadCount) HANDLE *m_CaptureThreadHandles;
	_Field_size_(m_CaptureThreadCount) CAPTURE_THREAD_DATA *m_CaptureThreadData;

	void Clean();
	void WaitForThreadTermination();
	//HRESULT DrawOverlay(_Inout_ ID3D11Texture2D *pBackgroundFrame, _In_ RECORDING_OVERLAY_DATA *pOverlay);
	//HRESULT DrawSource(_Inout_ ID3D11Texture2D *pBackgroundFrame, _In_ RECORDING_SOURCE_DATA *pSource);
	_Ret_maybenull_ CAPTURE_THREAD_DATA *GetCaptureDataForRect(RECT rect);
	RECT GetOverlayRect(_In_ SIZE canvasSize, _In_ SIZE overlayTextureSize, _In_ RECORDING_OVERLAY_DATA *pOverlay);
	RECT GetSourceRect(_In_ SIZE canvasSize, _In_ RECORDING_SOURCE_DATA *pSource);
};


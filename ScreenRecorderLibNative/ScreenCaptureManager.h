#pragma once
#include "CommonTypes.h"
#include "Log.h"
#include "DX.util.h"
#include "Screengrab.h"
#include "TextureManager.h"
#include "Util.h"

void ProcessCaptureHRESULT(_In_ HRESULT hr, _Inout_ CAPTURE_RESULT *pResult, _In_opt_ ID3D11Device *pDevice);

class ScreenCaptureManager
{
public:
	ScreenCaptureManager();
	virtual ~ScreenCaptureManager();
	virtual HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice, _In_ std::shared_ptr<OUTPUT_OPTIONS> pOutputOptions);
	virtual inline PTR_INFO *GetPointerInfo() {
		return &m_PtrInfo;
	}
	virtual RECT GetOutputRect() { return m_OutputRect; }
	virtual SIZE GetOutputSize() { return SIZE{ RectWidth(m_OutputRect),RectHeight(m_OutputRect) }; }
	virtual HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame);
	virtual HRESULT StartCapture(_In_ const std::vector<RECORDING_SOURCE*> &sources, _In_ const std::vector<RECORDING_OVERLAY*> &overlays, _In_  HANDLE hErrorEvent);
	virtual HRESULT StopCapture();
	virtual bool IsUpdatedFramesAvailable();
	virtual bool IsInitialFrameWriteComplete();
	virtual bool IsInitialOverlayWriteComplete();
	virtual bool IsCapturing() { return m_IsCapturing; }
	virtual UINT GetUpdatedFrameCount(_In_ bool resetUpdatedFrameCounts);
	std::vector<CAPTURE_THREAD_DATA> GetCaptureThreadData();
	std::vector<OVERLAY_THREAD_DATA> GetOverlayThreadData();
protected:
	LARGE_INTEGER m_LastAcquiredFrameTimeStamp;
	ID3D11Texture2D *m_SharedSurf;
	IDXGIKeyedMutex *m_KeyMutex;
	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	RECT m_OutputRect;
	PTR_INFO m_PtrInfo;

	virtual HRESULT CreateSharedSurf(_In_ RECT desktopRect, _Outptr_ ID3D11Texture2D **ppSharedTexture, _Outptr_ IDXGIKeyedMutex **ppKeyedMutex);
	virtual HRESULT CreateSharedSurf(_In_ const std::vector<RECORDING_SOURCE*> &sources, _Out_ std::vector<RECORDING_SOURCE_DATA *> *pCreatedOutputs, _Out_ RECT *pDeskBounds);
private:
	bool m_IsCapturing;
	HANDLE m_TerminateThreadsEvent;
	CRITICAL_SECTION m_CriticalSection;
	std::shared_ptr<OUTPUT_OPTIONS> m_OutputOptions;
	std::unique_ptr<TextureManager> m_TextureManager;

	UINT m_CaptureThreadCount;
	_Field_size_(m_CaptureThreadCount) HANDLE *m_CaptureThreadHandles;
	_Field_size_(m_CaptureThreadCount) CAPTURE_THREAD_DATA *m_CaptureThreadData;

	UINT m_OverlayThreadCount;
	_Field_size_(m_OverlayThreadCount) HANDLE *m_OverlayThreadHandles;
	_Field_size_(m_OverlayThreadCount) OVERLAY_THREAD_DATA *m_OverlayThreadData;

	void Clean();
	void WaitForThreadTermination();
	_Ret_maybenull_ CAPTURE_THREAD_DATA *GetCaptureDataForRect(RECT rect);
	RECT GetSourceRect(_In_ SIZE canvasSize, _In_ RECORDING_SOURCE_DATA *pSource);
	RECT GetOverlayRect(_In_ SIZE canvasSize, _In_ SIZE overlayTextureSize, _In_ RECORDING_OVERLAY *pOverlay);
	HRESULT ProcessOverlays(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Out_ int *updateCount);
};


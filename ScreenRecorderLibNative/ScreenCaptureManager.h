#pragma once
#include "CommonTypes.h"
#include "Log.h"
#include "DX.util.h"
#include "Screengrab.h"
#include "TextureManager.h"
#include "OverlayManager.h"
#include "Util.h"
using namespace ScreenRecorderLib::Overlays;

class ScreenCaptureManager
{
public:
	ScreenCaptureManager();
	virtual ~ScreenCaptureManager();
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
private:
	bool m_IsCapturing;
	HANDLE m_TerminateThreadsEvent;

	std::unique_ptr<TextureManager> m_TextureManager;
	std::unique_ptr<OverlayManager> m_OverlayManager;

	UINT m_CaptureThreadCount;
	_Field_size_(m_CaptureThreadCount) HANDLE *m_CaptureThreadHandles;
	_Field_size_(m_CaptureThreadCount) CAPTURE_THREAD_DATA *m_CaptureThreadData;

	void Clean();
	void WaitForThreadTermination();
	_Ret_maybenull_ CAPTURE_THREAD_DATA *GetCaptureDataForRect(RECT rect);
	RECT GetSourceRect(_In_ SIZE canvasSize, _In_ RECORDING_SOURCE_DATA *pSource);
};


#pragma once
#include "CommonTypes.h"
#include "Log.h"
#include "DX.util.h"
#include "Screengrab.h"
#include "TextureManager.h"
#include "Util.h"
#include <atlbase.h>

void ProcessCaptureHRESULT(_In_ HRESULT hr, _Inout_ CAPTURE_RESULT *pResult, _In_opt_ ID3D11Device *pDevice);

class ScreenCaptureManager
{
public:
	ScreenCaptureManager();
	virtual ~ScreenCaptureManager();
	virtual HRESULT Initialize(
		_In_ ID3D11DeviceContext *pDeviceContext,
		_In_ ID3D11Device *pDevice,
		_In_ std::shared_ptr<OUTPUT_OPTIONS> pOutputOptions,
		_In_ std::shared_ptr<ENCODER_OPTIONS> pEncoderOptions,
		_In_ std::shared_ptr<MOUSE_OPTIONS> pMouseOptions);
	virtual inline PTR_INFO *GetPointerInfo() {
		return &m_PtrInfo;
	}
	virtual RECT GetOutputRect() { return m_OutputRect; }
	virtual SIZE GetOutputSize() { return SIZE{ RectWidth(m_OutputRect),RectHeight(m_OutputRect) }; }
	virtual HRESULT CopyCurrentFrame(_Out_ CAPTURED_FRAME *pFrame);
	virtual HRESULT AcquireNextFrame(_In_  double timeUntilNextFrame, _In_ double maxFrameLength, _Out_ CAPTURED_FRAME *pFrame);
	virtual HRESULT StartCapture(_In_ const std::vector<RECORDING_SOURCE *> &sources, _In_ const std::vector<RECORDING_OVERLAY *> &overlays, _In_  HANDLE hErrorEvent);
	virtual HRESULT StopCapture();
	virtual bool IsUpdatedFramesAvailable();
	virtual bool IsInitialFrameWriteComplete();
	virtual bool IsInitialOverlayWriteComplete();
	virtual bool IsCapturing() { return m_IsCapturing; }
	virtual UINT GetUpdatedSourceCount();
	virtual UINT GetUpdatedOverlayCount();
	std::vector<CAPTURE_RESULT *> GetCaptureResults();
	std::vector<CAPTURE_THREAD_DATA> GetCaptureThreadData();
	std::vector<OVERLAY_THREAD_DATA> GetOverlayThreadData();
	virtual HRESULT ProcessOverlays(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Out_ int *updateCount);
	HRESULT InitializeOverlays(_In_ const std::vector<RECORDING_OVERLAY *> &overlays, _In_opt_  HANDLE hErrorEvent);
protected:
	LARGE_INTEGER m_LastAcquiredFrameTimeStamp;
	ID3D11Texture2D *m_SharedSurf;
	IDXGIKeyedMutex *m_KeyMutex;
	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	RECT m_OutputRect;
	PTR_INFO m_PtrInfo;

	virtual HRESULT CreateSharedSurf(_In_ RECT desktopRect, _Outptr_ ID3D11Texture2D **ppSharedTexture, _Outptr_ IDXGIKeyedMutex **ppKeyedMutex);
	virtual HRESULT CreateSharedSurf(_In_ const std::vector<RECORDING_SOURCE *> &sources, _Out_ std::vector<RECORDING_SOURCE_DATA *> *pCreatedOutputs, _Out_ RECT *pDeskBounds, _Outptr_ ID3D11Texture2D **ppSharedTexture, _Outptr_ IDXGIKeyedMutex **ppKeyedMutex);
private:
	bool m_IsInitialFrameWriteComplete;
	bool m_IsCapturing;
	HANDLE m_TerminateThreadsEvent;
	CRITICAL_SECTION m_CriticalSection;
	std::shared_ptr<ENCODER_OPTIONS> m_EncoderOptions;
	std::shared_ptr<OUTPUT_OPTIONS> m_OutputOptions;
	std::shared_ptr<MOUSE_OPTIONS> m_MouseOptions;
	std::unique_ptr<TextureManager> m_TextureManager;
	CComPtr<ID3D11Texture2D> m_FrameCopy;

	std::vector<CAPTURE_THREAD *> m_CaptureThreads;
	std::vector<OVERLAY_THREAD *> m_OverlayThreads;

	void Clean();
	HRESULT WaitForThreadTermination();
	_Ret_maybenull_ CAPTURE_THREAD_DATA *GetCaptureDataForRect(RECT rect);
	RECT GetSourceRect(_In_ SIZE canvasSize, _In_ RECORDING_SOURCE_DATA *pSource);
	RECT GetOverlayRect(_In_ SIZE canvasSize, _In_ SIZE overlayTextureSize, _In_ RECORDING_OVERLAY *pOverlay);
	HRESULT ScreenCaptureManager::InitializeRecordingSources(_In_ const std::vector<RECORDING_SOURCE_DATA *> &recordingSources, _In_opt_  HANDLE hErrorEvent);
};
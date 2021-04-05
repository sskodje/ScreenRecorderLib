#pragma once
#include <vector>
#include <sal.h>
#include <new>
#include <atlbase.h>
#include <algorithm>

#include "common_types.h"
#include "log.h"
#include "DX.util.h"
#include "utilities.h"
#include "screengrab.h"
#include "string_format.h"

class capture_base abstract
{
public:
	capture_base();
	virtual ~capture_base();
	virtual HRESULT Initialize(_In_ ID3D11DeviceContext* pDeviceContext, _In_ ID3D11Device* pDevice);
	virtual inline PTR_INFO* GetPointerInfo() {
		return &m_PtrInfo;
	}
	virtual RECT GetOutputRect() { return m_OutputRect; }
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
	ID3D11Texture2D* m_SharedSurf;
	IDXGIKeyedMutex* m_KeyMutex;
	ID3D11Device* m_Device;
	ID3D11DeviceContext* m_DeviceContext;
	RECT m_OutputRect;
	PTR_INFO m_PtrInfo;

	virtual HRESULT CreateSharedSurf(_In_ RECT desktopRect);
	virtual HRESULT CreateSharedSurf(_In_ std::vector<RECORDING_SOURCE> sources, _Out_ std::vector<RECORDING_SOURCE_DATA*> *pCreatedOutputs, _Out_ RECT* pDeskBounds);
	virtual SIZE GetContentSize();
	virtual RECT GetContentRect();
	virtual LPTHREAD_START_ROUTINE GetCaptureThreadProc() = 0;
	_Ret_maybenull_  HANDLE GetSharedHandle();
	HRESULT ProcessOverlays(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Out_ int *updateCount);
private:
	static const int NUMVERTICES = 6;
	bool m_IsCapturing;
	HANDLE m_TerminateThreadsEvent;
	ID3D11SamplerState *m_SamplerLinear;
	ID3D11BlendState *m_BlendState;
	ID3D11VertexShader *m_VertexShader;
	ID3D11PixelShader *m_PixelShader;
	ID3D11InputLayout *m_InputLayout;

	UINT m_OverlayThreadCount;
	_Field_size_(m_OverlayThreadCount) HANDLE* m_OverlayThreadHandles;
	_Field_size_(m_OverlayThreadCount) OVERLAY_THREAD_DATA* m_OverlayThreadData;

	UINT m_CaptureThreadCount;
	_Field_size_(m_CaptureThreadCount) HANDLE* m_CaptureThreadHandles;
	_Field_size_(m_CaptureThreadCount) CAPTURE_THREAD_DATA* m_CaptureThreadData;

	void Clean();
	void CleanDX();
	void WaitForThreadTermination();
	void ConfigureVertices(_Inout_ VERTEX (&vertices)[NUMVERTICES],_In_ RECORDING_OVERLAY_DATA *pOverlay, _In_ FRAME_INFO *pFrameInfo, _In_opt_ DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_UNSPECIFIED);
	HRESULT DrawOverlay(_Inout_ ID3D11Texture2D *pBackgroundFrame, _In_ RECORDING_OVERLAY_DATA *pOverlay);
	_Ret_maybenull_ CAPTURE_THREAD_DATA *GetCaptureDataForRect(RECT rect);
	RECT GetOverlayRect(_In_ RECT background, _In_ RECORDING_OVERLAY_DATA *pOverlay);
};


#pragma once
#include "common_types.h"
#include "log.h"
#include "DX.util.h"
#include "utilities.h"
#include <vector>
#include <sal.h>
#include <new>
#include <algorithm>

class capture_base
{
public:
	capture_base(_In_ ID3D11Device *pDevice, _In_ ID3D11DeviceContext *pDeviceContext);
	virtual ~capture_base();
	virtual inline PTR_INFO* GetPointerInfo() {
		return &m_PtrInfo;
	}
	virtual RECT GetOutputRect() { return m_OutputRect; }
	virtual HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame);
	virtual HRESULT StopCapture();
	virtual bool IsUpdatedFramesAvailable();
	virtual bool IsInitialFrameWriteComplete();
	virtual UINT GetUpdatedFrameCount(_In_ bool resetUpdatedFrameCounts);
protected:
	virtual HRESULT StartCapture(_In_ LPTHREAD_START_ROUTINE captureThreadProc, _In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
	virtual HRESULT StartOverlayCapture(_In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
	virtual HRESULT CreateSharedSurf(_In_ RECT desktopRect);
	virtual HRESULT CreateSharedSurf(_In_ std::vector<RECORDING_SOURCE> sources, _Out_ std::vector<RECORDING_SOURCE_DATA*> *pCreatedOutputs, _Out_ RECT* pDeskBounds);
	virtual SIZE GetContentSize();
	_Ret_maybenull_  HANDLE GetSharedHandle();
	HRESULT ProcessOverlays();

	LARGE_INTEGER m_LastAcquiredFrameTimeStamp;
	ID3D11Texture2D* m_SharedSurf;
	IDXGIKeyedMutex* m_KeyMutex;
	ID3D11Device* m_Device;
	ID3D11DeviceContext* m_DeviceContext;
	RECT m_OutputRect;
	PTR_INFO m_PtrInfo;
private:
	void Clean();
	void WaitForThreadTermination();
private:
	HANDLE m_TerminateThreadsEvent;

	UINT m_OverlayThreadCount;
	_Field_size_(m_OverlayThreadCount) HANDLE* m_OverlayThreadHandles;
	_Field_size_(m_OverlayThreadCount) OVERLAY_THREAD_DATA* m_OverlayThreadData;

	UINT m_CaptureThreadCount;
	_Field_size_(m_CaptureThreadCount) HANDLE* m_CaptureThreadHandles;
	_Field_size_(m_CaptureThreadCount) CAPTURE_THREAD_DATA* m_CaptureThreadData;
};


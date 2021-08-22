#pragma once
#include "CommonTypes.h"
#include "TextureManager.h"
#include <memory>

class OverlayManager
{
public:
	OverlayManager();
	~OverlayManager();
	virtual HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice);
	virtual HRESULT StartCapture(_In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
	virtual HRESULT StopCapture();
	virtual bool IsUpdatedFramesAvailable();
	HRESULT ProcessOverlays(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Out_ int *updateCount);
	virtual bool IsCapturing() { return m_IsCapturing; }
private:
	bool m_IsCapturing;
	HANDLE m_TerminateThreadsEvent;
	LARGE_INTEGER 
		m_LastAcquiredFrameTimeStamp;
	UINT m_OverlayThreadCount;
	_Field_size_(m_OverlayThreadCount) HANDLE *m_OverlayThreadHandles;
	_Field_size_(m_OverlayThreadCount) OVERLAY_THREAD_DATA *m_OverlayThreadData;

	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	std::unique_ptr<TextureManager> m_TextureManager;

	RECT GetOverlayRect(_In_ SIZE canvasSize, _In_ SIZE overlayTextureSize, _In_ RECORDING_OVERLAY_DATA *pOverlay);
	void Clean();
	void WaitForThreadTermination();
};


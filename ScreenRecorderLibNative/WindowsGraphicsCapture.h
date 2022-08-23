#pragma once
#include "ScreenCaptureBase.h"
#include "CommonTypes.h"
#include "TextureManager.h"
#include <memory>
#include "WindowsGraphicsCapture.util.h"
#include "MouseManager.h"
class WindowsGraphicsCapture : public CaptureBase
{
public:
	WindowsGraphicsCapture();
	virtual ~WindowsGraphicsCapture();
	virtual HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice) override;
	virtual HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame) override;
	virtual HRESULT WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect) override;
	virtual HRESULT StartCapture(_In_ RECORDING_SOURCE_BASE &recordingSource) override;
	virtual HRESULT StopCapture();
	virtual HRESULT GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *nativeMediaSize) override;
	virtual HRESULT GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ RECT frameCoordinates, _In_ int offsetX, _In_ int offsetY) override;
	virtual inline std::wstring Name() override { return L"WindowsGraphicsCapture"; };

private:
	void OnFrameArrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const &sender, winrt::Windows::Foundation::IInspectable const &args);
	HRESULT GetNextFrame(_In_ DWORD timeoutMillis, _Inout_ GRAPHICS_FRAME_DATA *pData);
	HRESULT GetCaptureItem(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ winrt::Windows::Graphics::Capture::GraphicsCaptureItem *item);
	HRESULT RecreateFramePool(_Inout_ GRAPHICS_FRAME_DATA *pData, _In_ winrt::Windows::Graphics::SizeInt32 newSize);
	HRESULT ProcessRecordingTimeout(_Inout_ GRAPHICS_FRAME_DATA *pData);
	bool IsRecordingSessionStale();
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_CaptureItem;
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool;
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session;

	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	std::unique_ptr<TextureManager> m_TextureManager;
	std::unique_ptr<MouseManager> m_MouseManager;
	RECORDING_SOURCE_BASE *m_RecordingSource;
	int m_CursorOffsetX;
	int m_CursorOffsetY;
	float m_CursorScaleX;
	float m_CursorScaleY;
	bool m_IsCursorCapturePropertyAvailable;
	bool m_IsInitialized;
	HANDLE m_NewFrameEvent;
	bool m_HaveDeliveredFirstFrame;
	std::atomic<bool> m_closed;
	RECT m_LastFrameRect;
	GRAPHICS_FRAME_DATA m_CurrentData;
	LARGE_INTEGER m_QPCFrequency;
	LARGE_INTEGER m_LastSampleReceivedTimeStamp;
	LARGE_INTEGER m_LastCaptureSessionRestart;
	LARGE_INTEGER m_LastGrabTimeStamp;

};
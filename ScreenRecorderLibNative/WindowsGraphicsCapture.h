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
	WindowsGraphicsCapture(_In_ bool isCursorCaptureEnabled);
	virtual ~WindowsGraphicsCapture();
	virtual HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice) override;
	virtual HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame) override;
	virtual HRESULT WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ const std::optional<RECT> &sourceRect = std::nullopt) override;
	virtual HRESULT StartCapture(_In_ RECORDING_SOURCE_BASE &recordingSource) override;
	virtual HRESULT StopCapture();
	virtual HRESULT GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *nativeMediaSize) override;
	virtual HRESULT GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ bool getShapeBuffer, _In_ RECT frameCoordinates, _In_ int offsetX, _In_ int offsetY) override;
	virtual inline std::wstring Name() override { return L"WindowsGraphicsCapture"; };

private:
	void OnFrameArrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const &sender, winrt::Windows::Foundation::IInspectable const &args);
	HRESULT WriteFrameUpdatesToSurface(_Inout_ GRAPHICS_FRAME_DATA *pData, _Inout_ ID3D11Texture2D *pSharedSurf, _In_  INT OffsetX, _In_  INT OffsetY, _In_ RECT &destinationRect, _In_opt_ const std::optional<RECT> &sourceRect = std::nullopt);
	HRESULT GetNextFrame(_In_ DWORD timeoutMillis, _Inout_ GRAPHICS_FRAME_DATA *pData);
private:
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem GetCaptureItem(_In_ RECORDING_SOURCE_BASE &recordingSource);
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_CaptureItem;
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool;
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session;

	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	std::unique_ptr<TextureManager> m_TextureManager;
	std::unique_ptr<MouseManager> m_MouseManager;
	RECORDING_SOURCE_BASE *m_RecordingSource;
	bool m_IsCursorCaptureEnabled;
	bool m_IsInitialized;
	HANDLE m_NewFrameEvent;
	bool m_HaveDeliveredFirstFrame;
	std::atomic<bool> m_closed;
	RECT m_LastFrameRect;
	GRAPHICS_FRAME_DATA m_CurrentData;
	LARGE_INTEGER m_LastSampleReceivedTimeStamp;
	LARGE_INTEGER m_LastGrabTimeStamp;
};
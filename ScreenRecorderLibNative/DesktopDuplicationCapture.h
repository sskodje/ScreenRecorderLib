#pragma once
#include "CommonTypes.h"
#include "ScreenCaptureBase.h"
#include <memory>
#include "MouseManager.h"
#include "DesktopDuplicationManager.h"

class DesktopDuplicationCapture : public CaptureBase
{
public:
	DesktopDuplicationCapture();
	DesktopDuplicationCapture(_In_ bool isCursorCaptureEnabled);
	virtual ~DesktopDuplicationCapture();
	virtual HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice) override;
	virtual HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame) override;
	virtual HRESULT WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ const std::optional<RECT> &sourceRect = std::nullopt) override;
	virtual HRESULT StartCapture(_In_ RECORDING_SOURCE_BASE &recordingSource) override;
	virtual HRESULT StopCapture();
	virtual HRESULT GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *nativeMediaSize) override;
	virtual HRESULT GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ bool getShapeBuffer, _In_ RECT frameCoordinates, _In_ int offsetX, _In_ int offsetY) override;
	virtual inline std::wstring Name() override { return L"DesktopDuplicationCapture"; };
private:
	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	std::unique_ptr<TextureManager> m_TextureManager;
	std::unique_ptr<DesktopDuplicationManager> m_DuplicationManager;
	std::unique_ptr<MouseManager> m_MouseManager;
	DUPL_FRAME_DATA m_CurrentData;

	bool m_IsCursorCaptureEnabled;
	bool m_IsInitialized;
	RecordingSourceType m_SourceType;
};

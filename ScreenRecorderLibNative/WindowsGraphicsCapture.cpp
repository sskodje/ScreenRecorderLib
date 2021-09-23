#include "WindowsGraphicsCapture.h"
#include "Util.h"
#include "Cleanup.h"
#include "MouseManager.h"

using namespace std;
using namespace Graphics::Capture::Util;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;

WindowsGraphicsCapture::WindowsGraphicsCapture() :
	CaptureBase(),
	m_CaptureItem(nullptr),
	m_SourceType(RecordingSourceType::Display),
	m_GraphicsManager(nullptr),
	m_IsInitialized(false),
	m_IsCursorCaptureEnabled(false),
	m_MouseManager(nullptr)
{
	RtlZeroMemory(&m_CurrentData, sizeof(m_CurrentData));
}

WindowsGraphicsCapture::WindowsGraphicsCapture(_In_ bool isCursorCaptureEnabled):WindowsGraphicsCapture()
{
	m_IsCursorCaptureEnabled = isCursorCaptureEnabled;
}

WindowsGraphicsCapture::~WindowsGraphicsCapture()
{
	SafeRelease(&m_Device);
	SafeRelease(&m_DeviceContext);
}
HRESULT WindowsGraphicsCapture::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_Device->AddRef();
	m_DeviceContext->AddRef();

	m_MouseManager = make_unique<MouseManager>();
	m_MouseManager->Initialize(pDeviceContext, pDevice, std::make_shared<MOUSE_OPTIONS>());

	if (m_Device && m_DeviceContext) {
		m_IsInitialized = true;
		return S_OK;
	}
	else {
		LOG_ERROR(L"WindowsGraphicsCapture initialization failed");
		return E_FAIL;
	}
}
HRESULT WindowsGraphicsCapture::AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame)
{
	if (!m_GraphicsManager) {
		LOG_ERROR("WindowsGraphicsManager must be initialized before frames can be fetched.");
		return E_FAIL;
	}
	// Get new frame from Windows Graphics Capture.
	HRESULT hr = m_GraphicsManager->GetFrame(timeoutMillis, &m_CurrentData);
	if (SUCCEEDED(hr) && ppFrame) {
		*ppFrame = m_CurrentData.Frame;
	}

	return hr;
}
HRESULT WindowsGraphicsCapture::WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ const std::optional<RECT> &sourceRect)
{
	if (!m_GraphicsManager) {
		LOG_ERROR("DesktopDuplicationManager must be initialized before frames can be fetched.");
		return E_FAIL;
	}
	return	m_GraphicsManager->ProcessFrame(&m_CurrentData, pSharedSurf, offsetX, offsetY, destinationRect, sourceRect);
}
HRESULT WindowsGraphicsCapture::StartCapture(_In_ RECORDING_SOURCE_BASE &recordingSource)
{
	if (!m_IsInitialized) {
		LOG_ERROR(L"Initialize must be called before StartCapture");
		return E_FAIL;
	}
	m_CaptureItem = GetCaptureItem(recordingSource);
	m_SourceType = recordingSource.Type;

	if (m_CaptureItem) {
		// Initialize graphics manager.
		m_GraphicsManager = make_unique<WindowsGraphicsManager>();
		return m_GraphicsManager->Initialize(m_DeviceContext, m_Device, m_CaptureItem, m_IsCursorCaptureEnabled, DirectXPixelFormat::B8G8R8A8UIntNormalized);
	}
	else {
		LOG_ERROR("Failed to create capture item");
		return E_FAIL;
	}
}
HRESULT WindowsGraphicsCapture::StopCapture()
{
	if (m_GraphicsManager) {
		m_GraphicsManager->Close();
		m_GraphicsManager.reset();
	}
	return S_OK;
}
HRESULT WindowsGraphicsCapture::GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *nativeMediaSize)
{
	if (!m_CaptureItem) {
		m_CaptureItem = GetCaptureItem(recordingSource);
	}
	if (!m_CaptureItem) {
		LOG_ERROR("GraphicsCaptureItem was NULL when a non-null value was expected");
		return E_FAIL;
	}
	*nativeMediaSize = SIZE{ m_CaptureItem.Size().Width,m_CaptureItem.Size().Height };
	return S_OK;
}
HRESULT WindowsGraphicsCapture::GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ bool getShapeBuffer, _In_ RECT frameCoordinates, _In_ int offsetX, _In_ int offsetY)
{
	// Windows Graphics Capture includes the mouse cursor on the texture, so we only get the positioning info for mouse click draws.
	return m_MouseManager->GetMouse(pPtrInfo, false, offsetX, offsetY);
}
GraphicsCaptureItem WindowsGraphicsCapture::GetCaptureItem(_In_ RECORDING_SOURCE_BASE &recordingSource)
{
	if (recordingSource.Type == RecordingSourceType::Window) {
		return CreateCaptureItemForWindow(recordingSource.SourceWindow);
	}
	else {
		CComPtr<IDXGIOutput> output = nullptr;
		HRESULT hr = GetOutputForDeviceName(recordingSource.SourcePath, &output);
		if (FAILED(hr)) {
			GetMainOutput(&output);
			if (!output) {
				LOG_ERROR("Failed to find any monitors to record");
				return nullptr;
			}
		}
		DXGI_OUTPUT_DESC outputDesc;
		output->GetDesc(&outputDesc);
		return CreateCaptureItemForMonitor(outputDesc.Monitor);
	}
	return nullptr;
}
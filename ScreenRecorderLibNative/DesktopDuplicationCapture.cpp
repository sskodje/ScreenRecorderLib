#include "DesktopDuplicationCapture.h"
#include "Cleanup.h"
#include "MouseManager.h"
#include "OverlayManager.h"
using namespace std::chrono;
using namespace std;

DesktopDuplicationCapture::DesktopDuplicationCapture() :
	CaptureBase(),
	m_SourceType(RecordingSourceType::Display),
	m_DuplicationManager(nullptr),
	m_IsInitialized(false),
	m_IsCursorCaptureEnabled(false)
{
	RtlZeroMemory(&m_CurrentData, sizeof(m_CurrentData));
}

DesktopDuplicationCapture::DesktopDuplicationCapture(_In_ bool isCursorCaptureEnabled) :DesktopDuplicationCapture()
{
	m_IsCursorCaptureEnabled = isCursorCaptureEnabled;
}

DesktopDuplicationCapture::~DesktopDuplicationCapture()
{
	SafeRelease(&m_Device);
	SafeRelease(&m_DeviceContext);
}

HRESULT DesktopDuplicationCapture::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_Device->AddRef();
	m_DeviceContext->AddRef();

	RtlZeroMemory(&m_CurrentData, sizeof(m_CurrentData));

	m_MouseManager = make_unique<MouseManager>();
	m_MouseManager->Initialize(pDeviceContext, pDevice, std::make_shared<MOUSE_OPTIONS>());

	if (m_Device && m_DeviceContext) {
		m_IsInitialized = true;
		return S_OK;
	}
	else {
		LOG_ERROR(L"DesktopDuplicationCapture initialization failed");
		return E_FAIL;
	}
}

HRESULT DesktopDuplicationCapture::AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame)
{
	if (!m_DuplicationManager) {
		LOG_ERROR("DesktopDuplicationManager must be initialized before frames can be fetched.");
		return E_FAIL;
	}
	// Get new frame from Desktop Duplication.
	HRESULT hr = m_DuplicationManager->GetFrame(timeoutMillis, &m_CurrentData);
	if (SUCCEEDED(hr) && ppFrame) {
		*ppFrame = m_CurrentData.Frame;
	}

	return hr;
}

HRESULT DesktopDuplicationCapture::WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ const std::optional<RECT> &sourceRect)
{
	if (!m_DuplicationManager) {
		LOG_ERROR("DesktopDuplicationManager must be initialized before frames can be fetched.");
		return E_FAIL;
	}
	return	m_DuplicationManager->ProcessFrame(&m_CurrentData, pSharedSurf, offsetX, offsetY, destinationRect, sourceRect);
}

HRESULT DesktopDuplicationCapture::StartCapture(_In_ RECORDING_SOURCE_BASE &recordingSource)
{
	// Make duplication manager
	m_DuplicationManager = make_unique<DesktopDuplicationManager>();
	HRESULT hr = m_DuplicationManager->Initialize(m_DeviceContext, m_Device, recordingSource.SourcePath);

	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed initialize DesktopDuplicationManager");
		return hr;
	}
	return hr;
}

HRESULT DesktopDuplicationCapture::StopCapture()
{
	if (m_DuplicationManager) {
		m_DuplicationManager.release();
		m_DuplicationManager = nullptr;
	}
	return S_OK;
}

HRESULT DesktopDuplicationCapture::GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *size)
{
	RtlZeroMemory(size, sizeof(size));
	CComPtr<IDXGIOutput> output;
	HRESULT hr = GetOutputForDeviceName(recordingSource.SourcePath, &output);
	if (output) {
		DXGI_OUTPUT_DESC desc;
		output->GetDesc(&desc);
		*size = SIZE{ RectWidth(desc.DesktopCoordinates),RectHeight(desc.DesktopCoordinates) };
	}
	return hr;
}
HRESULT DesktopDuplicationCapture::GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ bool getShapeBuffer, _In_ RECT frameCoordinates, _In_ int offsetX, _In_ int offsetY)
{
	return m_MouseManager->GetMouse(pPtrInfo, getShapeBuffer, &m_CurrentData.FrameInfo, frameCoordinates, m_DuplicationManager->GetOutputDuplication(), offsetX, offsetY);
}
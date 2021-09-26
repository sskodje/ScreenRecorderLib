#include "WindowsGraphicsCapture.h"
#include "Util.h"
#include "Cleanup.h"
#include "MouseManager.h"

using namespace std;
using namespace Graphics::Capture::Util;

namespace winrt
{
	using namespace Windows::Foundation;
	using namespace Windows::System;
	using namespace Windows::Graphics;
	using namespace Windows::Graphics::Capture;
	using namespace Windows::Graphics::DirectX;
	using namespace Windows::Graphics::DirectX::Direct3D11;
	using namespace Windows::Foundation::Numerics;
	using namespace Windows::UI;
	using namespace Windows::UI::Composition;
}

WindowsGraphicsCapture::WindowsGraphicsCapture() :
	CaptureBase(),
	m_CaptureItem(nullptr),
	m_SourceType(RecordingSourceType::Display),
	m_closed{ false },
	m_framePool(nullptr),
	m_session(nullptr),
	m_LastFrameRect{},
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_TextureManager(nullptr),
	m_HaveDeliveredFirstFrame(false),
	m_IsInitialized(false),
	m_IsCursorCaptureEnabled(false),
	m_MouseManager(nullptr),
	m_LastSampleReceivedTimeStamp{ 0 },
	m_LastGrabTimeStamp{ 0 }
{
	RtlZeroMemory(&m_CurrentData, sizeof(m_CurrentData));
	m_NewFrameEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

WindowsGraphicsCapture::WindowsGraphicsCapture(_In_ bool isCursorCaptureEnabled) :WindowsGraphicsCapture()
{
	m_IsCursorCaptureEnabled = isCursorCaptureEnabled;
}

WindowsGraphicsCapture::~WindowsGraphicsCapture()
{
	StopCapture();
	SafeRelease(&m_Device);
	SafeRelease(&m_DeviceContext);
	SafeRelease(&m_CurrentData.Frame);
	CloseHandle(m_NewFrameEvent);
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
	HRESULT hr = GetNextFrame(timeoutMillis, &m_CurrentData);

	//Must check for S_OK as the result can be S_FALSE for no updates
	if (SUCCEEDED(hr) && ppFrame) {
		D3D11_TEXTURE2D_DESC desc;
		m_CurrentData.Frame->GetDesc(&desc);
		ID3D11Texture2D *pFrame;
		m_Device->CreateTexture2D(&desc, nullptr, &pFrame);
		m_DeviceContext->CopyResource(pFrame, m_CurrentData.Frame);
		*ppFrame = pFrame;
		QueryPerformanceCounter(&m_LastGrabTimeStamp);
	}

	return hr;
}

HRESULT WindowsGraphicsCapture::WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ const std::optional<RECT> &sourceRect)
{
	HRESULT hr = S_OK;
	if (m_LastSampleReceivedTimeStamp.QuadPart >= m_CurrentData.Timestamp.QuadPart) {
		hr = GetNextFrame(timeoutMillis, &m_CurrentData);
	}
	//Must check for S_OK as the result can be S_FALSE for no updates
	if (hr == S_OK) {
		RETURN_ON_BAD_HR(hr = WriteFrameUpdatesToSurface(&m_CurrentData, pSharedSurf, offsetX, offsetY, destinationRect, sourceRect));
		QueryPerformanceCounter(&m_LastGrabTimeStamp);
	}
	return hr;
}

HRESULT WindowsGraphicsCapture::StartCapture(_In_ RECORDING_SOURCE_BASE &recordingSource)
{
	HRESULT hr = S_OK;
	if (!m_IsInitialized) {
		LOG_ERROR(L"Initialize must be called before StartCapture");
		return E_FAIL;
	}
	m_CaptureItem = GetCaptureItem(recordingSource);
	m_SourceType = recordingSource.Type;

	if (m_CaptureItem) {
		// Get DXGI device
		CComPtr<IDXGIDevice> DxgiDevice = nullptr;
		hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice));
		if (FAILED(hr))
		{
			LOG_ERROR(L"Failed to QI for DXGI Device");
			return hr;
		}
		auto direct3DDevice = Graphics::Capture::Util::CreateDirect3DDevice(DxgiDevice);
		// Creating our frame pool with 'Create' instead of 'CreateFreeThreaded'
		// means that the frame pool's FrameArrived event is called on the thread
		// the frame pool was created on. This also means that the creating thread
		// must have a DispatcherQueue. If you use this method, it's best not to do
		// it on the UI thread. 
		m_framePool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(direct3DDevice, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, m_CaptureItem.Size());
		m_session = m_framePool.CreateCaptureSession(m_CaptureItem);
		m_framePool.FrameArrived({ this, &WindowsGraphicsCapture::OnFrameArrived });

		WINRT_ASSERT(m_session != nullptr);
		m_session.IsCursorCaptureEnabled(m_IsCursorCaptureEnabled);
		m_session.StartCapture();
	}
	else {
		LOG_ERROR("Failed to create capture item");
		return E_FAIL;
	}
	return hr;
}

HRESULT WindowsGraphicsCapture::StopCapture()
{
	auto expected = false;
	if (m_closed.compare_exchange_strong(expected, true))
	{
		m_session.Close();
		m_framePool.Close();

		m_framePool = nullptr;
		m_session = nullptr;
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

winrt::GraphicsCaptureItem WindowsGraphicsCapture::GetCaptureItem(_In_ RECORDING_SOURCE_BASE &recordingSource)
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

void WindowsGraphicsCapture::OnFrameArrived(winrt::Direct3D11CaptureFramePool const &sender, winrt::IInspectable const &)
{
	QueryPerformanceCounter(&m_LastSampleReceivedTimeStamp);
	SetEvent(m_NewFrameEvent);
}

HRESULT WindowsGraphicsCapture::GetNextFrame(_In_ DWORD timeoutMillis, _Inout_ GRAPHICS_FRAME_DATA *pData)
{
	HRESULT hr = S_FALSE;
	DWORD result = WAIT_OBJECT_0;
	if (pData->Timestamp.QuadPart >= m_LastSampleReceivedTimeStamp.QuadPart) {
		result = WaitForSingleObject(m_NewFrameEvent, timeoutMillis);
	}
	if (result == WAIT_OBJECT_0) {
		winrt::Direct3D11CaptureFrame frame = m_framePool.TryGetNextFrame();
		if (frame) {
			MeasureExecutionTime measureGetFrame(L"WindowsGraphicsManager::GetNextFrame");
			auto surfaceTexture = Graphics::Capture::Util::GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
			D3D11_TEXTURE2D_DESC desc;
			surfaceTexture->GetDesc(&desc);

			if (FAILED(hr))
			{
				LOG_ERROR(L"Failed to create texture");
				return hr;
			}
			if (frame.ContentSize().Width != pData->ContentSize.cx
					|| frame.ContentSize().Height != pData->ContentSize.cy) {
				//The source has changed size, so we must recreate the frame pool with the new size.
				CComPtr<IDXGIDevice> DxgiDevice = nullptr;
				hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice));
				if (FAILED(hr))
				{
					LOG_ERROR(L"Failed to QI for DXGI Device");
					return hr;
				}
				SafeRelease(&pData->Frame);
				hr = m_Device->CreateTexture2D(&desc, nullptr, &pData->Frame);
				measureGetFrame.SetName(L"WindowsGraphicsManager::GetNextFrame recreated");
				auto direct3DDevice = Graphics::Capture::Util::CreateDirect3DDevice(DxgiDevice);
				m_framePool.Recreate(direct3DDevice, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, frame.ContentSize());
				//Some times the size of the first frame is wrong when recording windows, so we just skip it and get a new after resizing the frame pool.
				if (pData->IsWindow
					&& !m_HaveDeliveredFirstFrame) {
					m_HaveDeliveredFirstFrame = true;
					frame.Close();
					return GetNextFrame(timeoutMillis, pData);
				}
			}
			m_DeviceContext->CopyResource(pData->Frame, surfaceTexture.get());
			pData->ContentSize.cx = frame.ContentSize().Width;
			pData->ContentSize.cy = frame.ContentSize().Height;
			m_HaveDeliveredFirstFrame = true;
			QueryPerformanceCounter(&pData->Timestamp);
			frame.Close();
		}
	}
	else if (result == WAIT_TIMEOUT) {
		hr = DXGI_ERROR_WAIT_TIMEOUT;
	}
	else {
		DWORD dwErr = GetLastError();
		LOG_ERROR(L"WaitForSingleObject failed: last error = %u", dwErr);
		hr = HRESULT_FROM_WIN32(dwErr);
	}
	return hr;
}

HRESULT WindowsGraphicsCapture::BlankFrame(_Inout_ ID3D11Texture2D *pSharedSurf, _In_ RECT rect, _In_ INT offsetX, _In_  INT offsetY) {
	int width = RectWidth(rect);
	int height = RectHeight(rect);
	D3D11_BOX Box{};
	// Copy back to shared surface
	Box.right = width;
	Box.bottom = height;
	Box.back = 1;

	CComPtr<ID3D11Texture2D> pBlankFrame;
	D3D11_TEXTURE2D_DESC desc;
	pSharedSurf->GetDesc(&desc);
	desc.MiscFlags = 0;
	desc.Width = width;
	desc.Height = height;
	HRESULT hr = m_Device->CreateTexture2D(&desc, nullptr, &pBlankFrame);
	if (SUCCEEDED(hr)) {
		m_DeviceContext->CopySubresourceRegion(pSharedSurf, 0, rect.left + offsetX, rect.top + offsetY, 0, pBlankFrame, 0, &Box);
	}
	return S_OK;
}

HRESULT WindowsGraphicsCapture::WriteFrameUpdatesToSurface(_Inout_ GRAPHICS_FRAME_DATA *pData, _Inout_ ID3D11Texture2D *pSharedSurf, _In_ INT offsetX, _In_  INT offsetY, _In_ RECT &destinationRect, _In_opt_ const std::optional<RECT> &sourceRect)
{
	HRESULT hr = S_FALSE;
	if (pData->IsIconic) {
		BlankFrame(pSharedSurf, MakeRectEven(destinationRect), offsetX, offsetY);
		return hr;
	}
	CComPtr<ID3D11Texture2D> processedTexture = pData->Frame;
	D3D11_TEXTURE2D_DESC frameDesc;
	processedTexture->GetDesc(&frameDesc);

	if (sourceRect.has_value()
		&& IsValidRect(sourceRect.value())
		&& (RectWidth(sourceRect.value()) != frameDesc.Width || (RectHeight(sourceRect.value()) != frameDesc.Height))) {
		ID3D11Texture2D *pCroppedTexture;
		RETURN_ON_BAD_HR(hr = m_TextureManager->CropTexture(processedTexture, sourceRect.value(), &pCroppedTexture));
		processedTexture.Release();
		processedTexture.Attach(pCroppedTexture);
	}
	processedTexture->GetDesc(&frameDesc);

	int leftMargin = 0;
	int topMargin = 0;
	if ((RectWidth(destinationRect) != frameDesc.Width || RectHeight(destinationRect) != frameDesc.Height)) {
		double widthRatio = (double)RectWidth(destinationRect) / frameDesc.Width;
		double heightRatio = (double)RectHeight(destinationRect) / frameDesc.Height;

		double resizeRatio = min(widthRatio, heightRatio);
		UINT resizedWidth = (UINT)MakeEven((LONG)round(frameDesc.Width * resizeRatio));
		UINT resizedHeight = (UINT)MakeEven((LONG)round(frameDesc.Height * resizeRatio));
		ID3D11Texture2D *resizedTexture = nullptr;
		RETURN_ON_BAD_HR(hr = m_TextureManager->ResizeTexture(processedTexture, &resizedTexture, SIZE{ static_cast<LONG>(resizedWidth), static_cast<LONG>(resizedHeight) }));
		processedTexture.Release();
		processedTexture.Attach(resizedTexture);
		leftMargin = (int)max(0, round(((double)RectWidth(destinationRect) - (double)resizedWidth)) / 2);
		topMargin = (int)max(0, round(((double)RectHeight(destinationRect) - (double)resizedHeight)) / 2);
	}

	processedTexture->GetDesc(&frameDesc);

	RECT finalFrameRect = MakeRectEven(RECT
		{
			destinationRect.left,
			destinationRect.top,
			destinationRect.left + (LONG)frameDesc.Width,
			destinationRect.top + (LONG)frameDesc.Height
		});

	if (!IsRectEmpty(&m_LastFrameRect) && !EqualRect(&finalFrameRect, &m_LastFrameRect)) {
		BlankFrame(pSharedSurf, m_LastFrameRect, offsetX, offsetY);
	}

	D3D11_BOX Box;
	Box.front = 0;
	Box.back = 1;
	Box.left = 0;
	Box.top = 0;
	Box.right = MakeEven(frameDesc.Width);
	Box.bottom = MakeEven(frameDesc.Height);

	m_DeviceContext->CopySubresourceRegion(pSharedSurf, 0, finalFrameRect.left + offsetX + leftMargin, finalFrameRect.top + offsetY + topMargin, 0, processedTexture, 0, &Box);
	m_LastFrameRect = finalFrameRect;
	return S_OK;
}
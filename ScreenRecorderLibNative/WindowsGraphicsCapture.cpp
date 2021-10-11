#include "WindowsGraphicsCapture.h"
#include "Util.h"
#include <dwmapi.h>
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
	m_closed{ true },
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
	m_LastGrabTimeStamp{ 0 },
	m_RecordingSource(nullptr)
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
	HRESULT hr = m_MouseManager->Initialize(pDeviceContext, pDevice, std::make_shared<MOUSE_OPTIONS>());

	m_TextureManager = make_unique<TextureManager>();
	hr = m_TextureManager->Initialize(pDeviceContext, pDevice);

	if (m_Device && m_DeviceContext) {
		m_IsInitialized = true;
		return S_OK;
	}
	else {
		LOG_ERROR(L"WindowsGraphicsCapture initialization failed");
		return E_FAIL;
	}
	return hr;
}

HRESULT WindowsGraphicsCapture::AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame)
{
	HRESULT hr = GetNextFrame(timeoutMillis, &m_CurrentData);

	if (SUCCEEDED(hr) && ppFrame) {
		D3D11_TEXTURE2D_DESC desc;
		m_CurrentData.Frame->GetDesc(&desc);
		ID3D11Texture2D *pFrame= nullptr;
		hr = m_Device->CreateTexture2D(&desc, nullptr, &pFrame);
		if (SUCCEEDED(hr)) {
			m_DeviceContext->CopyResource(pFrame, m_CurrentData.Frame);
			QueryPerformanceCounter(&m_LastGrabTimeStamp);
		}
		*ppFrame = pFrame;
	}

	return hr;
}

HRESULT WindowsGraphicsCapture::WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ const std::optional<RECT> &sourceRect)
{
	HRESULT hr = S_OK;
	if (m_LastSampleReceivedTimeStamp.QuadPart >= m_CurrentData.Timestamp.QuadPart) {
		hr = GetNextFrame(timeoutMillis, &m_CurrentData);
	}
	if (m_closed) {
		return E_ABORT;
	}
	if (SUCCEEDED(hr)) {
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
	m_RecordingSource = &recordingSource;
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
		m_closed.store(false);
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
	switch (recordingSource.Type)
	{
		case RecordingSourceType::Window:
		{
			RECT windowRect{};
			if (IsIconic(recordingSource.SourceWindow)) {
				WINDOWPLACEMENT placement;
				placement.length = sizeof(WINDOWPLACEMENT);
				if (GetWindowPlacement(recordingSource.SourceWindow, &placement)) {
					windowRect = placement.rcNormalPosition;
					RECT rcWind;
					//While GetWindowPlacement gets us the dimensions of the minimized window, they include invisible borders we don't want.
					//To remove the borders, we check the difference between DwmGetWindowAttribute and GetWindowRect, which gives us the combined left and right borders.
					//Then the border offset is removed from the left,right and bottom of the window rect.
					GetWindowRect(recordingSource.SourceWindow, &rcWind);
					RECT windowAttrRect{};
					long offset = 0;
					if (SUCCEEDED(DwmGetWindowAttribute(recordingSource.SourceWindow, DWMWA_EXTENDED_FRAME_BOUNDS, &windowAttrRect, sizeof(windowRect))))
					{
						offset = RectWidth(windowAttrRect) - RectWidth(rcWind);
					}
					windowRect.bottom += offset / 2;
					windowRect.right += offset;
					//Offset the window rect to start at[0,0] instead of screen coordinates.
					OffsetRect(&windowRect, -windowRect.left, -windowRect.top);
				}
			}
			else
			{
				if (SUCCEEDED(DwmGetWindowAttribute(recordingSource.SourceWindow, DWMWA_EXTENDED_FRAME_BOUNDS, &windowRect, sizeof(windowRect))))
				{
					//Offset the window rect to start at[0,0] instead of screen coordinates.
					OffsetRect(&windowRect, -windowRect.left, -windowRect.top);
				}
			}
			*nativeMediaSize = SIZE{ RectWidth(windowRect),RectHeight(windowRect) };
			break;
		}
		case RecordingSourceType::Display: {
			if (!m_CaptureItem) {
				m_CaptureItem = GetCaptureItem(recordingSource);
			}
			if (!m_CaptureItem) {
				LOG_ERROR("GraphicsCaptureItem was NULL when a non-null value was expected");
				return E_FAIL;
			}
			*nativeMediaSize = SIZE{ m_CaptureItem.Size().Width,m_CaptureItem.Size().Height };
			break;
		}
		default:
			*nativeMediaSize = SIZE{};
			break;
	}

	return S_OK;
}

HRESULT WindowsGraphicsCapture::GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ RECT frameCoordinates, _In_ int offsetX, _In_ int offsetY)
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
	HRESULT hr = E_FAIL;
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
				if (FAILED(hr))
				{
					LOG_ERROR(L"Failed to create texture");
					return hr;
				}
				measureGetFrame.SetName(L"WindowsGraphicsManager::GetNextFrame recreated");
				auto direct3DDevice = Graphics::Capture::Util::CreateDirect3DDevice(DxgiDevice);
				/*
				* If the recording is started on a minimized window, we will have guesstimated a size for it when starting the recording.
				* In this instance we continue to use this size instead of the Direct3D11CaptureFrame::ContentSize(), as it may differ by a few pixels
				* due to windows 10 window borders and trigger a resize, which leads to blurry recordings.
				*/
				winrt::SizeInt32 newSize = pData->ContentSize.cx > 0 ? winrt::SizeInt32{ pData->ContentSize.cx,pData->ContentSize.cy } : frame.ContentSize();
				m_framePool.Recreate(direct3DDevice, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, newSize);
				//Some times the size of the first frame is wrong when recording windows, so we just skip it and get a new after resizing the frame pool.
				if (m_RecordingSource->Type == RecordingSourceType::Window
					&& !m_HaveDeliveredFirstFrame) {
					m_HaveDeliveredFirstFrame = true;
					frame.Close();
					return GetNextFrame(timeoutMillis, pData);
				}
				pData->ContentSize.cx = frame.ContentSize().Width;
				pData->ContentSize.cy = frame.ContentSize().Height;
			}

			m_DeviceContext->CopyResource(pData->Frame, surfaceTexture.get());
			m_HaveDeliveredFirstFrame = true;
			QueryPerformanceCounter(&pData->Timestamp);
			frame.Close();
			hr = S_OK;
		}
		else {
			hr = DXGI_ERROR_WAIT_TIMEOUT;
		}
	}
	else if (result == WAIT_TIMEOUT) {
		if (m_RecordingSource->Type == RecordingSourceType::Window && IsIconic(m_RecordingSource->SourceWindow)) {
			SIZE frameSize;
			if (!pData->Frame) {
				RETURN_ON_BAD_HR(GetNativeSize(*m_RecordingSource, &frameSize));
				D3D11_TEXTURE2D_DESC desc;
				RtlZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
				desc.Width = frameSize.cx;
				desc.Height = frameSize.cy;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pData->Frame));
			}
			else {
				D3D11_TEXTURE2D_DESC desc;
				pData->Frame->GetDesc(&desc);
				frameSize = SIZE{ static_cast<long>(desc.Width),static_cast<long>(desc.Height) };
			}
			pData->ContentSize = frameSize;
			m_TextureManager->BlankTexture(pData->Frame, RECT{ 0,0,frameSize.cx,frameSize.cy }, 0, 0);
			QueryPerformanceCounter(&pData->Timestamp);
			hr = S_OK;
		}
		else {
			hr = DXGI_ERROR_WAIT_TIMEOUT;
		}
	}
	else {
		DWORD dwErr = GetLastError();
		LOG_ERROR(L"WaitForSingleObject failed: last error = %u", dwErr);
		hr = HRESULT_FROM_WIN32(dwErr);
	}
	return hr;
}

HRESULT WindowsGraphicsCapture::WriteFrameUpdatesToSurface(_Inout_ GRAPHICS_FRAME_DATA *pData, _Inout_ ID3D11Texture2D *pSharedSurf, _In_ INT offsetX, _In_  INT offsetY, _In_ RECT &destinationRect, _In_opt_ const std::optional<RECT> &sourceRect)
{
	HRESULT hr = S_OK;

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
	if (RectWidth(destinationRect) != frameDesc.Width || RectHeight(destinationRect) != frameDesc.Height) {
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
		m_TextureManager->BlankTexture(pSharedSurf, MakeRectEven(destinationRect), offsetX, offsetY);
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
	return hr;
}
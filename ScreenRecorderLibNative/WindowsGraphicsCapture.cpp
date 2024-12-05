#include "WindowsGraphicsCapture.h"
#include "Util.h"
#include <dwmapi.h>
#include "Cleanup.h"
#include "MouseManager.h"
using namespace DirectX;

#pragma comment(lib,"runtimeobject.lib")

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
	m_HaveDeliveredFirstFrame(false),
	m_IsInitialized(false),
	m_MouseManager(nullptr),
	m_QPCFrequency{ 0 },
	m_LastSampleReceivedTimeStamp{ 0 },
	m_LastCaptureSessionRestart{ 0 },
	m_CursorOffsetX(0),
	m_CursorOffsetY(0),
	m_CursorScaleX(1.0),
	m_CursorScaleY(1.0)
{
	RtlZeroMemory(&m_CurrentData, sizeof(m_CurrentData));
	m_NewFrameEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	QueryPerformanceFrequency(&m_QPCFrequency);
}

WindowsGraphicsCapture::~WindowsGraphicsCapture()
{
	StopCapture();
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
	RETURN_ON_BAD_HR(hr);
	m_TextureManager = make_unique<TextureManager>();
	RETURN_ON_BAD_HR(hr = m_TextureManager->Initialize(pDeviceContext, pDevice));
	if (SUCCEEDED(hr)) {
		m_IsInitialized = true;
	}
	return hr;
}

HRESULT WindowsGraphicsCapture::AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame)
{
	HRESULT hr = GetNextFrame(timeoutMillis, &m_CurrentData);

	if (SUCCEEDED(hr) && ppFrame) {
		QueryPerformanceCounter(&m_LastGrabTimeStamp);
		*ppFrame = m_CurrentData.Frame;
		(*ppFrame)->AddRef();
	}

	return hr;
}

HRESULT WindowsGraphicsCapture::WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ ID3D11Texture2D *pTexture)
{
	HRESULT hr = S_OK;
	if (pTexture) {
		m_CurrentData.Frame = pTexture;
		QueryPerformanceCounter(&m_CurrentData.Timestamp);
		D3D11_TEXTURE2D_DESC desc;
		pTexture->GetDesc(&desc);
		m_CurrentData.ContentSize = SIZE{ static_cast<long>(desc.Width),static_cast<long>(desc.Height) };
		hr = S_OK;
	}
	else if (m_LastSampleReceivedTimeStamp.QuadPart >= m_CurrentData.Timestamp.QuadPart) {
		hr = GetNextFrame(timeoutMillis, &m_CurrentData);
	}
	if (m_closed) {
		return E_ABORT;
	}
	if (SUCCEEDED(hr)) {
		CComPtr<ID3D11Texture2D> pProcessedTexture = m_CurrentData.Frame;
		D3D11_TEXTURE2D_DESC frameDesc;
		pProcessedTexture->GetDesc(&frameDesc);
		RECORDING_SOURCE *recordingSource = dynamic_cast<RECORDING_SOURCE *>(m_RecordingSource);
		if (!recordingSource) {
			LOG_ERROR("Recording source cannot be NULL");
			return E_FAIL;
		}
		int cursorOffsetX = 0;
		int cursorOffsetY = 0;
		float cursorScaleX = 1.0;
		float cursorScaleY = 1.0;

		if (recordingSource->SourceRect.has_value()
			&& IsValidRect(recordingSource->SourceRect.value())
			&& (RectWidth(recordingSource->SourceRect.value()) != frameDesc.Width || (RectHeight(recordingSource->SourceRect.value()) != frameDesc.Height))) {
			ID3D11Texture2D *pCroppedTexture;
			RETURN_ON_BAD_HR(hr = m_TextureManager->CropTexture(pProcessedTexture, recordingSource->SourceRect.value(), &pCroppedTexture));
			if (hr == S_OK) {
				pProcessedTexture.Release();
				pProcessedTexture.Attach(pCroppedTexture);
			}
			pProcessedTexture->GetDesc(&frameDesc);
			cursorOffsetX = 0 - recordingSource->SourceRect.value().left;
			cursorOffsetY = 0 - recordingSource->SourceRect.value().top;
		}
		RECT contentRect = destinationRect;
		if (m_RecordingSource
			&& (RectWidth(destinationRect) != frameDesc.Width || RectHeight(destinationRect) != frameDesc.Height)) {
			ID3D11Texture2D *pResizedTexture;
			RETURN_ON_BAD_HR(hr = m_TextureManager->ResizeTexture(pProcessedTexture, SIZE{ RectWidth(destinationRect),RectHeight(destinationRect) }, m_RecordingSource->Stretch, &pResizedTexture, &contentRect));
			int prescaleWidth = frameDesc.Width;
			int prescaleHeight = frameDesc.Height;
			pProcessedTexture.Release();
			pProcessedTexture.Attach(pResizedTexture);
			pProcessedTexture->GetDesc(&frameDesc);
			cursorScaleX = (float)RectWidth(contentRect) / prescaleWidth;
			cursorScaleY = (float)RectHeight(contentRect) / prescaleHeight;
		}

		RECT finalFrameRect = MakeRectEven(RECT
			{
				destinationRect.left,
				destinationRect.top,
				destinationRect.left + (LONG)RectWidth(contentRect),
				destinationRect.top + (LONG)RectHeight(contentRect)
			});

		if (!IsRectEmpty(&m_LastFrameRect) && !EqualRect(&finalFrameRect, &m_LastFrameRect)) {
			m_TextureManager->BlankTexture(pSharedSurf, MakeRectEven(destinationRect), offsetX, offsetY);
		}

		SIZE contentOffset = GetContentOffset(recordingSource->Anchor, destinationRect, contentRect);
		cursorOffsetX += static_cast<int>(round(contentOffset.cx / cursorScaleX));
		cursorOffsetY += static_cast<int>(round(contentOffset.cy / cursorScaleY));

		D3D11_TEXTURE2D_DESC desc;
		pSharedSurf->GetDesc(&desc);

		LONG textureOffsetX = finalFrameRect.left + offsetX + contentOffset.cx;
		LONG textureOffsetY = finalFrameRect.top + offsetY + contentOffset.cy;

		D3D11_BOX Box;
		Box.front = 0;
		Box.back = 1;
		Box.left = 0;
		Box.top = 0;
		Box.right = RectWidth(contentRect);
		Box.bottom = RectHeight(contentRect);

		if (textureOffsetX + Box.right > desc.Width) {
			Box.right = desc.Width - textureOffsetX;
		}
		if (textureOffsetY + Box.bottom > desc.Height) {
			Box.bottom = desc.Height - textureOffsetY;
		}
		m_DeviceContext->CopySubresourceRegion(pSharedSurf, 0, textureOffsetX, textureOffsetY, 0, pProcessedTexture, 0, &Box);
		m_LastFrameRect = finalFrameRect;
		m_CursorOffsetX = cursorOffsetX;
		m_CursorOffsetY = cursorOffsetY;
		m_CursorScaleX = cursorScaleX;
		m_CursorScaleY = cursorScaleY;
		QueryPerformanceCounter(&m_LastGrabTimeStamp);
		SendBitmapCallback(pProcessedTexture);
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
	m_RecordingSource = &recordingSource;
	hr = GetCaptureItem(recordingSource, &m_CaptureItem);
	if (SUCCEEDED(hr)) {
		// Get DXGI device
		CComPtr<IDXGIDevice> DxgiDevice = nullptr;
		hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice));
		if (FAILED(hr))
		{
			LOG_ERROR(L"Failed to QI for DXGI Device");
			return hr;
		}
		try
		{
			auto direct3DDevice = Graphics::Capture::Util::CreateDirect3DDevice(DxgiDevice);
			// Creating our frame pool with 'Create' instead of 'CreateFreeThreaded'
			// means that the frame pool's FrameArrived event is called on the thread
			// the frame pool was created on. This also means that the creating thread
			// must have a DispatcherQueue. If you use this method, it's best not to do
			// it on the UI thread. 
			m_framePool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(direct3DDevice, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, m_CaptureItem.Size());

			m_session = m_framePool.CreateCaptureSession(m_CaptureItem);

			if (IsGraphicsCaptureIsBorderRequiredPropertyAvailable()) {
				m_session.IsBorderRequired(recordingSource.IsBorderRequired.value_or(true));
			}

			m_framePool.FrameArrived({ this, &WindowsGraphicsCapture::OnFrameArrived });

			WINRT_ASSERT(m_session != nullptr);
			if (IsGraphicsCaptureCursorCapturePropertyAvailable()) {
				m_session.IsCursorCaptureEnabled(m_RecordingSource->IsCursorCaptureEnabled.value_or(true));
			}
			m_session.StartCapture();
			m_closed.store(false);
		}
		catch (winrt::hresult_error const &ex)
		{
			hr = ex.code();
			LOG_ERROR(L"Failed to create WindowsGraphicsCapture session: error is %ls", ex.message().c_str());
		}
	}
	return hr;
}

HRESULT WindowsGraphicsCapture::StopCapture()
{
	HRESULT hr = S_OK;
	auto expected = false;
	if (m_closed.compare_exchange_strong(expected, true))
	{
		try
		{
			m_session.Close();
		}
		catch (winrt::hresult_error const &ex)
		{
			hr = ex.code();
			LOG_ERROR(L"Failed to close WindowsGraphicsCapture session: error is %ls", ex.message().c_str());
		}
		try
		{
			m_framePool.Close();
		}
		catch (winrt::hresult_error const &ex)
		{
			hr = ex.code();
			LOG_ERROR(L"Failed to close WindowsGraphicsCapture frame pool: error is %ls", ex.message().c_str());
		}
		m_framePool = nullptr;
		m_session = nullptr;
	}
	return hr;
}

HRESULT WindowsGraphicsCapture::GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *nativeMediaSize)
{
	HRESULT hr = S_OK;
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
						offset = RectWidth(rcWind) - RectWidth(windowAttrRect);
					}
					windowRect.bottom -= offset / 2;
					windowRect.right -= offset;
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
				hr = GetCaptureItem(recordingSource, &m_CaptureItem);
			}
			if (SUCCEEDED(hr))
			{
				*nativeMediaSize = SIZE{ m_CaptureItem.Size().Width,m_CaptureItem.Size().Height };
			}
			else {
				LOG_ERROR("GraphicsCaptureItem was NULL when a non-null value was expected");
			}
			break;
		}
		default:
			*nativeMediaSize = SIZE{};
			break;
	}

	return hr;
}

HRESULT WindowsGraphicsCapture::GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ RECT frameCoordinates, _In_ int offsetX, _In_ int offsetY)
{
	// Windows Graphics Capture includes the mouse cursor on the texture, so we only get the positioning info for mouse click draws.
	HRESULT hr = m_MouseManager->GetMouse(pPtrInfo, false, offsetX, offsetY);
	pPtrInfo->Scale = SIZE_F{ m_CursorScaleX, m_CursorScaleY };
	pPtrInfo->Offset = POINT{ m_CursorOffsetX, m_CursorOffsetY };
	return hr;
}

HRESULT WindowsGraphicsCapture::GetCaptureItem(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ winrt::GraphicsCaptureItem *item)
{
	HRESULT hr = S_OK;
	if (recordingSource.Type == RecordingSourceType::Window) {
		try
		{
			*item = CreateCaptureItemForWindow(recordingSource.SourceWindow);
		}
		catch (winrt::hresult_error const &ex)
		{
			hr = ex.code();
			LOG_ERROR(L"Failed to create capture item for window: error is %ls", ex.message().c_str());
		}
	}
	else {
		CComPtr<IDXGIOutput> output = nullptr;
		hr = GetOutputForDeviceName(recordingSource.SourcePath, &output);
		if (FAILED(hr)) {
			_com_error err(hr);
			LOG_ERROR(L"Failed to get output %ls in WindowsGraphicsCapture: %ls", recordingSource.SourcePath.c_str(), err.ErrorMessage());
			return hr;
		}
		DXGI_OUTPUT_DESC outputDesc;
		output->GetDesc(&outputDesc);
		try
		{
			*item = CreateCaptureItemForMonitor(outputDesc.Monitor);
		}
		catch (winrt::hresult_error const &ex)
		{
			hr = ex.code();
			LOG_ERROR(L"Failed to create capture item for monitor: error is %ls", ex.message().c_str());
		}
	}
	return hr;
}

HRESULT WindowsGraphicsCapture::RecreateFramePool(_Inout_ GRAPHICS_FRAME_DATA *pData, _In_ winrt::SizeInt32 newSize)
{
	//The source has changed size, so we must recreate the frame pool with the new size.
	CComPtr<IDXGIDevice> DxgiDevice = nullptr;
	HRESULT hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to QI for DXGI Device");
		return hr;
	}

	try
	{
		auto direct3DDevice = Graphics::Capture::Util::CreateDirect3DDevice(DxgiDevice);
		winrt::SizeInt32 newFramePoolSize = newSize;

		/// If recording a window, make the frame pool return a slightly larger texture that we crop later.
		/// This prevents issues with clipping when resizing windows.
		if (m_RecordingSource->Type == RecordingSourceType::Window) {
			newFramePoolSize.Width += 100;
			newFramePoolSize.Height += 100;
		}
		m_framePool.Recreate(direct3DDevice, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, newFramePoolSize);
		LOG_TRACE(L"Recreated WGC Frame Pool size [%d,%d]", newFramePoolSize.Width, newFramePoolSize.Height);
	}
	catch (winrt::hresult_error const &ex)
	{
		hr = ex.code();
		LOG_ERROR(L"Failed to recreate WindowsGraphicsCapture frame pool: error is %ls", ex.message().c_str());
		return hr;
	}
	pData->ContentSize.cx = newSize.Width;
	pData->ContentSize.cy = newSize.Height;
	return hr;
}

HRESULT WindowsGraphicsCapture::ProcessRecordingTimeout(_Inout_ GRAPHICS_FRAME_DATA *pData)
{
	if (m_RecordingSource->Type == RecordingSourceType::Window) {
		if (!IsWindow(m_RecordingSource->SourceWindow)) {
			//The window is gone, gracefully abort.
			return E_ABORT;
		}
		else if (IsIconic(m_RecordingSource->SourceWindow)) {
			//IsIconic means the window is minimized, and not rendered, so a blank placeholder texture is used instead.
			SIZE windowSize;
			if (!pData->Frame) {
				RETURN_ON_BAD_HR(GetNativeSize(*m_RecordingSource, &windowSize));
				D3D11_TEXTURE2D_DESC desc;
				RtlZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
				desc.Width = windowSize.cx;
				desc.Height = windowSize.cy;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				RETURN_ON_BAD_HR(m_Device->CreateTexture2D(&desc, nullptr, &pData->Frame));
			}
			else {
				D3D11_TEXTURE2D_DESC desc;
				pData->Frame->GetDesc(&desc);
				windowSize = SIZE{ static_cast<long>(desc.Width),static_cast<long>(desc.Height) };
			}
			pData->ContentSize = windowSize;
			m_TextureManager->BlankTexture(pData->Frame, RECT{ 0,0,windowSize.cx,windowSize.cy });
			QueryPerformanceCounter(&pData->Timestamp);
			return S_OK;
		}
		else if (IsRecordingSessionStale()) {
			//The session has stopped producing frames for a while, so it should be restarted.
			RETURN_ON_BAD_HR(StopCapture());
			RETURN_ON_BAD_HR(StartCapture(*m_RecordingSource));
			LOG_INFO("Restarted Windows Graphics Capture");
			QueryPerformanceCounter(&m_LastCaptureSessionRestart);
		}
	}
	return DXGI_ERROR_WAIT_TIMEOUT;
}

/// <summary>
/// Check if no new frames have been received for a while.
/// This issue could be caused by some window operations bugging out WGC.
/// </summary>
/// <returns>True is frame pool have stopped receiving frames, else false</returns>
bool WindowsGraphicsCapture::IsRecordingSessionStale()
{
	LARGE_INTEGER currentTime;
	QueryPerformanceCounter(&currentTime);
	double millisSinceLastFrameReceive = double(currentTime.QuadPart - m_LastSampleReceivedTimeStamp.QuadPart) / (m_QPCFrequency.QuadPart / 1000);
	if (millisSinceLastFrameReceive > 250) {
		double millisSinceLastCaptureRestart = double(currentTime.QuadPart - m_LastCaptureSessionRestart.QuadPart) / (m_QPCFrequency.QuadPart / 1000);
		if (millisSinceLastCaptureRestart > 500) {
			return true;
		}
	}
	return false;
}

void WindowsGraphicsCapture::OnFrameArrived(winrt::Direct3D11CaptureFramePool const &sender, winrt::IInspectable const &)
{
	QueryPerformanceCounter(&m_LastSampleReceivedTimeStamp);
	SetEvent(m_NewFrameEvent);
	if (m_session.IsCursorCaptureEnabled() != m_RecordingSource->IsCursorCaptureEnabled.value_or(true)) {
		m_session.IsCursorCaptureEnabled(m_RecordingSource->IsCursorCaptureEnabled.value_or(true));
	}
}

HRESULT WindowsGraphicsCapture::GetNextFrame(_In_ DWORD timeoutMillis, _Inout_ GRAPHICS_FRAME_DATA *pData)
{
	HRESULT hr = E_FAIL;
	DWORD result = WAIT_OBJECT_0;
	if (pData->Timestamp.QuadPart >= m_LastSampleReceivedTimeStamp.QuadPart) {
		result = WaitForSingleObject(m_NewFrameEvent, timeoutMillis);
	}
	if (result == WAIT_OBJECT_0) {
		winrt::Direct3D11CaptureFrame frame = nullptr;
		try
		{
			frame = m_framePool.TryGetNextFrame();
		}
		catch (winrt::hresult_error const &ex)
		{
			hr = ex.code();
			LOG_ERROR(L"Failed to get Direct3D11CaptureFrame: error is %ls", ex.message().c_str());
			return hr;
		}
		if (frame) {
			MeasureExecutionTime measureGetFrame(L"WindowsGraphicsManager::GetNextFrame");
			if (frame.ContentSize().Width != pData->ContentSize.cx
					|| frame.ContentSize().Height != pData->ContentSize.cy) {

				RETURN_ON_BAD_HR(hr = RecreateFramePool(pData, frame.ContentSize()));

				/*
				* If the recording is started on a minimized window, we will have guesstimated a size for it when starting the recording.
				* In this instance we continue to use this size instead of the Direct3D11CaptureFrame::ContentSize(), as it may differ by a few pixels
				* due to windows 10 window borders and trigger a resize, which leads to blurry recordings.
				*/
				auto newFrameSize = (!m_HaveDeliveredFirstFrame && pData->ContentSize.cx > 0) ? winrt::SizeInt32{ pData->ContentSize.cx, pData->ContentSize.cy } : frame.ContentSize();
				auto surfaceTexture = Graphics::Capture::Util::GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
				D3D11_TEXTURE2D_DESC newFrameDesc;
				surfaceTexture->GetDesc(&newFrameDesc);
				newFrameDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				newFrameDesc.Width = newFrameSize.Width;
				newFrameDesc.Height = newFrameSize.Height;
				SafeRelease(&pData->Frame);
				hr = m_Device->CreateTexture2D(&newFrameDesc, nullptr, &pData->Frame);
				if (FAILED(hr))
				{
					LOG_ERROR(L"Failed to create texture");
					return hr;
				}

				measureGetFrame.SetName(L"WindowsGraphicsManager::GetNextFrame recreated");

				//Some times the size of the first frame is wrong when recording windows, so we just skip it and get a new after resizing the frame pool.
				if (m_RecordingSource->Type == RecordingSourceType::Window
					&& !m_HaveDeliveredFirstFrame)
				{
					m_HaveDeliveredFirstFrame = true;
					frame.Close();
					return GetNextFrame(timeoutMillis, pData);
				}
			}

			D3D11_TEXTURE2D_DESC desc;
			pData->Frame->GetDesc(&desc);

			D3D11_BOX sourceRegion;
			RtlZeroMemory(&sourceRegion, sizeof(sourceRegion));
			sourceRegion.left = 0;
			sourceRegion.right = min(frame.ContentSize().Width, (int)desc.Width);
			sourceRegion.top = 0;
			sourceRegion.bottom = min(frame.ContentSize().Height, (int)desc.Height);
			sourceRegion.front = 0;
			sourceRegion.back = 1;
			auto surfaceTexture = Graphics::Capture::Util::GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
			m_DeviceContext->CopySubresourceRegion(pData->Frame, 0, 0, 0, 0, surfaceTexture.get(), 0, &sourceRegion);
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
		hr = ProcessRecordingTimeout(pData);
	}
	else {
		DWORD dwErr = GetLastError();
		LOG_ERROR(L"WaitForSingleObject failed: last error = %u", dwErr);
		hr = HRESULT_FROM_WIN32(dwErr);
	}
	return hr;
}
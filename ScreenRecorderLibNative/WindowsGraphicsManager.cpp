#include "WindowsGraphicsManager.h"
#include "Log.h"
#include "DX.util.h"
#include "WindowsGraphicsCapture.util.h"
#include "Cleanup.h"

using namespace winrt::Windows::Graphics::Capture;

WindowsGraphicsManager::WindowsGraphicsManager() :
	m_closed{ false },
	m_item(nullptr),
	m_framePool(nullptr),
	m_session(nullptr),
	m_LastFrameRect{},
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_PixelFormat{}
{

}

WindowsGraphicsManager::~WindowsGraphicsManager()
{
	Close();
	CleanRefs();
}

//
// Clean all references
//
void WindowsGraphicsManager::CleanRefs()
{
	if (m_DeviceContext)
	{
		m_DeviceContext->Release();
		m_DeviceContext = nullptr;
	}

	if (m_Device)
	{
		m_Device->Release();
		m_Device = nullptr;
	}
}


HRESULT WindowsGraphicsManager::Initialize(_In_ DX_RESOURCES *pData, _In_ winrt::Windows::Graphics::Capture::GraphicsCaptureItem captureItem, _In_ bool isCursorCaptureEnabled, _In_ winrt::Windows::Graphics::DirectX::DirectXPixelFormat pixelFormat)
{
	m_item = captureItem;
	m_PixelFormat = pixelFormat;
	m_Device = pData->Device;
	m_DeviceContext = pData->Context;

	m_Device->AddRef();
	m_DeviceContext->AddRef();

	// Get DXGI device
	CComPtr<IDXGIDevice> DxgiDevice = nullptr;
	HRESULT hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice));
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
	m_framePool = Direct3D11CaptureFramePool::Create(direct3DDevice, m_PixelFormat, 1, m_item.Size());
	m_session = m_framePool.CreateCaptureSession(m_item);

	WINRT_ASSERT(m_session != nullptr);
	m_session.IsCursorCaptureEnabled(isCursorCaptureEnabled);
	m_session.StartCapture();
	return S_OK;
}

HRESULT WindowsGraphicsManager::ProcessFrame(_In_ GRAPHICS_FRAME_DATA *pData, _Inout_ ID3D11Texture2D *pSharedSurf, _In_ INT OffsetX, _In_  INT OffsetY, _In_ RECT &frameRect)
{
	if (pData->IsIconic) {
		BlankFrame(pSharedSurf, OffsetX, OffsetY);
		return S_OK;
	}

	SIZE frameSize = SIZE{ RectWidth(frameRect), RectHeight(frameRect) };
	if (frameSize.cx < RectWidth(m_LastFrameRect)
		|| frameSize.cy < RectHeight(m_LastFrameRect)) {
		BlankFrame(pSharedSurf, OffsetX, OffsetY);
	}
	D3D11_BOX Box;
	// Copy back to shared surface
	Box.left = 0;
	Box.top = 0;
	Box.front = 0;
	Box.right = frameSize.cx;
	Box.bottom = frameSize.cy;
	Box.back = 1;
	m_DeviceContext->CopySubresourceRegion(pSharedSurf, 0, frameRect.left + OffsetX, frameRect.top + OffsetY, 0, pData->Frame, 0, &Box);
	m_LastFrameRect = frameRect;
	return S_OK;
}

HRESULT WindowsGraphicsManager::BlankFrame(_Inout_ ID3D11Texture2D *pSharedSurf, _In_ INT OffsetX, _In_  INT OffsetY) {
	int width = RectWidth(m_LastFrameRect);
	int height = RectHeight(m_LastFrameRect);
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
		m_DeviceContext->CopySubresourceRegion(pSharedSurf, 0, m_LastFrameRect.left + OffsetX, m_LastFrameRect.top + OffsetY, 0, pBlankFrame, 0, &Box);
	}
	return S_OK;
}

HRESULT WindowsGraphicsManager::GetFrame(_Out_ GRAPHICS_FRAME_DATA *pData)
{
	Direct3D11CaptureFrame frame = m_framePool.TryGetNextFrame();
	if (frame) {
		auto surfaceTexture = Graphics::Capture::Util::GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
		pData->Frame = surfaceTexture.get();
		pData->ContentSize.cx = frame.ContentSize().Width;
		pData->ContentSize.cy = frame.ContentSize().Height;
		frame.Close();
		return S_OK;
	}
	return DXGI_ERROR_WAIT_TIMEOUT;
}

SIZE WindowsGraphicsManager::ItemSize()
{
	SIZE size{ };
	size.cx = m_item.Size().Width;
	size.cy = m_item.Size().Height;
	return size;
}

void WindowsGraphicsManager::Close()
{
	auto expected = false;
	if (m_closed.compare_exchange_strong(expected, true))
	{
		m_session.Close();
		m_framePool.Close();

		m_framePool = nullptr;
		m_session = nullptr;
	}
}
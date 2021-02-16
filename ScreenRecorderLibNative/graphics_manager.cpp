#include "graphics_manager.h"
#include "log.h"
#include "DX.util.h"
#include "graphics_capture.util.h"

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

using namespace winrt::Windows::Graphics::Capture;

graphics_manager::graphics_manager() :
	m_item(nullptr),
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_PixelFormat{}
{

}

graphics_manager::~graphics_manager()
{
	Close();
	CleanRefs();
}

//
// Clean all references
//
void graphics_manager::CleanRefs()
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


HRESULT graphics_manager::Initialize(_In_ DX_RESOURCES *Data, _In_ winrt::Windows::Graphics::Capture::GraphicsCaptureItem captureItem, _In_ bool isCursorCaptureEnabled, _In_ winrt::Windows::Graphics::DirectX::DirectXPixelFormat pixelFormat)
{
	m_item = captureItem;
	m_PixelFormat = pixelFormat;
	m_Device = Data->Device;
	m_DeviceContext = Data->Context;

	m_Device->AddRef();
	m_DeviceContext->AddRef();

	// Get DXGI device
	IDXGIDevice* DxgiDevice = nullptr;
	HRESULT hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to QI for DXGI Device");
		return hr;
	}
	auto direct3DDevice = capture::util::CreateDirect3DDevice(DxgiDevice);
	// Creating our frame pool with 'Create' instead of 'CreateFreeThreaded'
	// means that the frame pool's FrameArrived event is called on the thread
	// the frame pool was created on. This also means that the creating thread
	// must have a DispatcherQueue. If you use this method, it's best not to do
	// it on the UI thread. 
	m_framePool = winrt::Direct3D11CaptureFramePool::Create(direct3DDevice, m_PixelFormat, 2, m_item.Size());
	m_session = m_framePool.CreateCaptureSession(m_item);

	WINRT_ASSERT(m_session != nullptr);
	m_session.IsCursorCaptureEnabled(isCursorCaptureEnabled);
	m_session.StartCapture();
	return S_OK;
}

HRESULT graphics_manager::ProcessFrame(_In_ GRAPHICS_FRAME_DATA* Data, _Inout_ ID3D11Texture2D* SharedSurf, _In_ INT OffsetX, _In_  INT OffsetY, _In_ RECT &frameRect)
{
	D3D11_BOX Box;
	// Copy back to shared surface
	Box.left = 0;
	Box.top = 0;
	Box.front = 0;
	Box.right = frameRect.right-frameRect.left;
	Box.bottom = frameRect.bottom-frameRect.top;
	Box.back = 1;
	m_DeviceContext->CopySubresourceRegion(SharedSurf, 0, frameRect.left - OffsetX, frameRect.top - OffsetY, 0, Data->Frame, 0, &Box);
	return S_OK;
}

HRESULT graphics_manager::GetFrame(_Out_ GRAPHICS_FRAME_DATA *Data)
{
	Direct3D11CaptureFrame frame = m_framePool.TryGetNextFrame();
	if (frame) {
		auto surfaceTexture = capture::util::GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
		Data->Frame = surfaceTexture.get();
		Data->ContentSize.cx = frame.ContentSize().Width;
		Data->ContentSize.cy = frame.ContentSize().Height;
		frame.Close();
		return S_OK;
	}
	return DXGI_ERROR_WAIT_TIMEOUT;
}

SIZE graphics_manager::ItemSize()
{
	SIZE size{ };
	size.cx = m_item.Size().Width;
	size.cy = m_item.Size().Height;
	return size;
}

void graphics_manager::Close()
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
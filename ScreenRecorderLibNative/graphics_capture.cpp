#include "graphics_capture.h"
#include "graphics_capture.util.h"
#include <atlbase.h>
#include "utilities.h"
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

using namespace capture::util;
using namespace winrt::Windows::Graphics::Capture;

graphics_capture::graphics_capture(winrt::IDirect3DDevice const & device, winrt::GraphicsCaptureItem const & item, winrt::DirectXPixelFormat pixelFormat, std::function<void(winrt::Direct3D11CaptureFrame)> frameArrivedCallback)
{
	m_item = item;
	m_Device = device;
	m_PixelFormat = pixelFormat;
	m_FrameArrivedCallback = frameArrivedCallback;
	auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_Device);

	d3dDevice->GetImmediateContext(m_ImmediateContext.put());

	// Creating our frame pool with 'Create' instead of 'CreateFreeThreaded'
	// means that the frame pool's FrameArrived event is called on the thread
	// the frame pool was created on. This also means that the creating thread
	// must have a DispatcherQueue. If you use this method, it's best not to do
	// it on the UI thread. 
	m_framePool = winrt::Direct3D11CaptureFramePool::Create(m_Device, m_PixelFormat, 2, m_item.Size());
	m_session = m_framePool.CreateCaptureSession(m_item);
	m_lastSize = m_item.Size();
	m_framePool.FrameArrived({ this, &graphics_capture::OnFrameArrived });

	WINRT_ASSERT(m_session != nullptr);

}

void graphics_capture::StartCapture()
{
	CheckClosed();
	m_session.StartCapture();
}


void graphics_capture::Close()
{
	auto expected = false;
	if (m_closed.compare_exchange_strong(expected, true))
	{
		m_session.Close();
		m_framePool.Close();

		m_framePool = nullptr;
		m_session = nullptr;
		m_item = nullptr;
	}
}

void graphics_capture::OnFrameArrived(winrt::Direct3D11CaptureFramePool const& sender, winrt::IInspectable const&)
{
	{
		auto frame = sender.TryGetNextFrame();
		if (m_FrameArrivedCallback)
			m_FrameArrivedCallback(frame);
	}
}

#pragma once
#include <functional>

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d2d1_3.h>
#include <wincodec.h>

#include <Unknwn.h>
#include <inspectable.h>

// WinRT
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Popups.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3d11.h>

class graphics_capture
{
public:
	graphics_capture(
		winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
		winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
		winrt::Windows::Graphics::DirectX::DirectXPixelFormat pixelFormat,
		std::function<void(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame)> frameArrivedCallback);
	~graphics_capture() { Close(); }

	void StartCapture();
	void ClearFrameBuffer() { while (m_framePool.TryGetNextFrame() != nullptr) {}; }
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem CaptureItem() { return m_item; }
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame TryGetNextFrame() { return m_framePool.TryGetNextFrame(); }
	void Close();

private:
	void OnFrameArrived(
		winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
		winrt::Windows::Foundation::IInspectable const& args);

	inline void CheckClosed()
	{
		if (m_closed.load() == true)
		{
			throw winrt::hresult_error(RO_E_CLOSED);
		}
	}

private:
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
	winrt::Windows::Graphics::SizeInt32 m_lastSize;

	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_Device{ nullptr };
	winrt::com_ptr<ID3D11DeviceContext> m_ImmediateContext{ nullptr };
	winrt::Windows::Graphics::DirectX::DirectXPixelFormat m_PixelFormat;

	std::atomic<bool> m_closed = false;
	std::function<void(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame)> m_FrameArrivedCallback;
};
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

#include "common_types.h"

class graphics_manager
{
public:
	graphics_manager();
	~graphics_manager();
	void Close();
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem CaptureItem() { return m_item; }
	HRESULT Initialize(_In_ DX_RESOURCES *Data, _In_ winrt::Windows::Graphics::Capture::GraphicsCaptureItem Output, _In_ bool isCursorCaptureEnabled, _In_ winrt::Windows::Graphics::DirectX::DirectXPixelFormat pixelFormat);
	HRESULT ProcessFrame(_In_ GRAPHICS_FRAME_DATA *Data, _Inout_ ID3D11Texture2D *SharedSurf, _In_  INT OffsetX, _In_  INT OffsetY, _In_ RECT &DeskDesc);
	HRESULT GetFrame(_Out_ GRAPHICS_FRAME_DATA *Data);
	SIZE ItemSize();
private:
	inline void CheckClosed()
	{
		if (m_closed.load() == true)
		{
			throw winrt::hresult_error(RO_E_CLOSED);
		}
	}
	void CleanRefs();

private:
	winrt::Windows::Graphics::DirectX::DirectXPixelFormat m_PixelFormat;
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };

	std::atomic<bool> m_closed = false;

	ID3D11Device* m_Device;
	ID3D11DeviceContext* m_DeviceContext;
};
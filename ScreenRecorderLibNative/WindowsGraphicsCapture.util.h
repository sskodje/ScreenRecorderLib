#pragma once
#include <inspectable.h>
#include <winrt/windows.graphics.directx.direct3d11.h>
#include <dxgi.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.capture.h>
namespace Graphics::Capture::Util
{
	HRESULT CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice *dxgiDevice,::IInspectable **graphicsDevice);

	struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
		IDirect3DDxgiInterfaceAccess : ::IUnknown
	{
		virtual HRESULT __stdcall GetInterface(GUID const &id, void **object) = 0;
	};

	inline auto CreateDirect3DDevice(IDXGIDevice *dxgi_device)
	{
		winrt::com_ptr<::IInspectable> d3d_device;
		winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi_device, d3d_device.put()));
		return d3d_device.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
	}

	template <typename T>
	auto GetDXGIInterfaceFromObject(winrt::Windows::Foundation::IInspectable const &object)
	{
		auto access = object.as<IDirect3DDxgiInterfaceAccess>();
		winrt::com_ptr<T> result;
		winrt::check_hresult(access->GetInterface(winrt::guid_of<T>(), result.put_void()));
		return result;
	}

	inline auto CreateCaptureItemForWindow(HWND hwnd)
	{
		auto interop_factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
		winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = { nullptr };
		winrt::check_hresult(interop_factory->CreateForWindow(hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), winrt::put_abi(item)));
		return item;
	}

	inline auto CreateCaptureItemForMonitor(HMONITOR hmon)
	{
		auto interop_factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
		winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = { nullptr };
		winrt::check_hresult(interop_factory->CreateForMonitor(hmon, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), winrt::put_abi(item)));
		return item;
	}
	inline bool IsGraphicsCaptureAvailable() {
		try
		{
			return winrt::Windows::Foundation::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 8)
				&& winrt::Windows::Foundation::Metadata::ApiInformation::IsTypePresent(L"Windows.Graphics.Capture.GraphicsCaptureSession")
				&& winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
		}
		catch (winrt::hresult_error err) {
			return false;
		}
	}

	inline bool IsGraphicsCaptureCursorCapturePropertyAvailable() {
		try
		{
			return IsGraphicsCaptureAvailable()
				&& winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled");
		}
		catch (winrt::hresult_error err) {
			return false;
		}
	}
}

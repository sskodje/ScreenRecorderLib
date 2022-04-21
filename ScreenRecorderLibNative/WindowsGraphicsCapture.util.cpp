#include "WindowsGraphicsCapture.util.h"

namespace Graphics::Capture::Util
{
	using _CreateDirect3D11DeviceFromDXGIDevice = HRESULT __stdcall(IDXGIDevice *, IInspectable **);

	HRESULT CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice *dxgiDevice, IInspectable **graphicsDevice) {
		auto libraryModule = LoadLibraryA("D3D11.dll");
		HRESULT hr = E_FAIL;
		if (libraryModule != nullptr)
		{
			auto addr = GetProcAddress(libraryModule, "CreateDirect3D11DeviceFromDXGIDevice");
			if (addr != nullptr)
			{
				auto createDirect3D11DeviceFromDXGIDevice = reinterpret_cast<_CreateDirect3D11DeviceFromDXGIDevice *>(addr);
				hr = createDirect3D11DeviceFromDXGIDevice(dxgiDevice, graphicsDevice);
			}
			FreeLibrary(libraryModule);
		}
		return hr;
	}
}
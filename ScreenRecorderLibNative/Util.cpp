#include "util.h"

using _GetDpiForSystem = UINT __stdcall();

/// <summary>
/// Returns the system DPI from GetDpiForSystem() on Windows 10 v1607 or above, 
/// or falls back to using GetDeviceCaps for earlier windows versions.
/// </summary>
/// <returns></returns>
UINT GetSystemDpi()
{
	auto libraryModule = LoadLibraryA("User32.dll");
	HRESULT hr = E_FAIL;
	int dpi = 96;
	if (libraryModule != nullptr)
	{
		auto addr = GetProcAddress(libraryModule, "GetDpiForSystem");
		if (addr != nullptr)
		{
			auto GetDpi = reinterpret_cast<_GetDpiForSystem *>(addr);
			dpi = GetDpi();
		}
		else {
			auto dc = GetDC(nullptr);
			dpi = GetDeviceCaps(dc, LOGPIXELSX);
			ReleaseDC(nullptr, dc);
		}
		FreeLibrary(libraryModule);
	}

	return dpi;
}
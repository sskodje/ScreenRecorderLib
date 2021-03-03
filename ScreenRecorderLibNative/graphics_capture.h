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
#include "DX.util.h"
#include "mouse_pointer.h"
#include "capture_base.h"

class graphics_capture : public capture_base
{
public:
	graphics_capture(_In_ ID3D11Device* pDevice, _In_ ID3D11DeviceContext *pDeviceContext, _In_ bool isCursorCaptureEnabled);
	~graphics_capture();
	HRESULT StartCapture(std::vector<std::wstring> const& sources, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
	HRESULT StartCapture(HWND windowhandle, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
	HRESULT AcquireNextFrame(_In_  DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame);
private:
	HRESULT CreateSharedSurf(_In_ HWND windowhandle, _Out_ RECT* pDeskBounds);
	HRESULT CreateSharedSurf(_In_ std::vector<std::wstring> outputs, _Out_ std::vector<std::wstring> *pOutputs, _Out_ std::vector<SIZE> *pOffsets, _Out_ RECT* pDeskBounds);



private:
	bool m_isCursorCaptureEnabled = false;
};
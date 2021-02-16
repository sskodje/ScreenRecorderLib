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

class graphics_capture
{
public:
	graphics_capture(_In_ ID3D11Device* pDevice, _In_ ID3D11DeviceContext *pDeviceContext, _In_ bool isCursorCaptureEnabled);
	~graphics_capture();
	void Clean();
	HRESULT StartCapture(std::vector<std::wstring> const& outputs, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
	HRESULT StartCapture(HWND windowhandle, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
	SIZE FrameSize();
	HRESULT AcquireNextFrame(_In_  DWORD timeoutMillis, _Out_ CAPTURED_FRAME *frame);
	void WaitForThreadTermination();
	HRESULT StopCapture();
private:
	HRESULT CreateSharedSurf(_In_ HWND windowhandle, _Out_ RECT* pDeskBounds);
	HRESULT CreateSharedSurf(_In_ std::vector<std::wstring> outputs, _Out_ std::vector<std::wstring> *createdOutputs, _Out_ RECT* pDeskBounds);
	HANDLE GetSharedHandle();



private:
	bool m_isCursorCaptureEnabled = false;
	ID3D11DeviceContext *m_ImmediateContext;
	ID3D11Device *m_Device;
	HANDLE m_TerminateThreadsEvent;
	LARGE_INTEGER m_LastAcquiredFrameTimeStamp;
	RECT m_OutputRect;
	ID3D11Texture2D* m_SharedSurf;
	IDXGIKeyedMutex* m_KeyMutex;
	UINT m_ThreadCount;
	_Field_size_(m_ThreadCount) HANDLE* m_ThreadHandles;
	_Field_size_(m_ThreadCount) THREAD_DATA* m_ThreadData;
};
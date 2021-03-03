#pragma once
#include "common_types.h"
#include "log.h"
#include "DX.util.h"
#include "utilities.h"
#include <vector>
#include <sal.h>
#include <new>
#include <algorithm>

DWORD WINAPI OverlayProc(_In_ void* Param);

class capture_base
{
public:
	capture_base(_In_ ID3D11Device *pDevice, _In_ ID3D11DeviceContext *pDeviceContext);
	~capture_base();

	void Clean();
	inline PTR_INFO* GetPointerInfo() {
		return &m_PtrInfo;
	}
	void WaitForThreadTermination();
	RECT GetOutputRect() { return m_OutputRect; }
	virtual HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame);
	HRESULT StopCapture();

protected:
	HRESULT CreateSharedSurf(_In_ RECT desktopRect);
	HANDLE GetSharedHandle();

	HANDLE m_TerminateThreadsEvent;
	LARGE_INTEGER m_LastAcquiredFrameTimeStamp;
	ID3D11Texture2D* m_SharedSurf;
	IDXGIKeyedMutex* m_KeyMutex;
	ID3D11Device* m_Device;
	ID3D11DeviceContext* m_DeviceContext;
	RECT m_OutputRect;
	PTR_INFO m_PtrInfo;
	UINT m_ThreadCount;
	_Field_size_(m_ThreadCount) HANDLE* m_ThreadHandles;
	_Field_size_(m_ThreadCount) THREAD_DATA* m_ThreadData;
};


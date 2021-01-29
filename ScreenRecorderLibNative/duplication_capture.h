#pragma once
#include "common_types.h"
#include <sal.h>
#include "log.h"
#include "utilities.h"
#include <vector>
class duplication_capture
{
public:
	duplication_capture(_In_ ID3D11Device *pDevice, _In_ ID3D11DeviceContext *pImmediateContext);
	~duplication_capture();
	void Clean();
	HRESULT StartCapture(_In_ std::vector<std::wstring> outputs, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
	HRESULT StopCapture();
	PTR_INFO* GetPointerInfo();
	HRESULT AcquireNextFrame(_In_ ID3D11Texture2D **ppDesktopFrame, _In_ DWORD timeoutMillis, _Out_ int &updatedFrameCount);
	void WaitForThreadTermination();
	RECT GetOutputRect() { return m_OutputRect; }
private:
	HRESULT InitializeDx(_Out_ DX_RESOURCES *Data);
	void CleanDx(_Inout_ DX_RESOURCES* Data);
	HRESULT CreateSharedSurf(_In_ std::vector<std::wstring> outputs, _Out_ std::vector<std::wstring> *createdOutputs, _Out_ RECT* pDeskBounds);
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

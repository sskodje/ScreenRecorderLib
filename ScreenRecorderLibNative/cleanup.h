// cleanup.h
#pragma once
#include <audioclient.h>
#include "WWMFResampler.h"
#include <atlbase.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include "log.h"
#include "duplication_capture.h"
template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = nullptr;
	}
}


class DeleteFileOnExit
{
public:
	DeleteFileOnExit(ATL::CComPtr<IWICStream>& hFile, LPCWSTR szFile) noexcept : m_filename(szFile), m_handle(hFile) {}

	DeleteFileOnExit(const DeleteFileOnExit&) = delete;
	DeleteFileOnExit& operator=(const DeleteFileOnExit&) = delete;

	DeleteFileOnExit(const DeleteFileOnExit&&) = delete;
	DeleteFileOnExit& operator=(const DeleteFileOnExit&&) = delete;

	~DeleteFileOnExit()
	{
		if (m_filename)
		{
			m_handle.Release();
			DeleteFileW(m_filename);
		}
	}

	void clear() noexcept { m_filename = nullptr; }

private:
	LPCWSTR m_filename;
	ATL::CComPtr<IWICStream>& m_handle;
};

class DesktopDuplicationCaptureStopOnExit {
public:
	DesktopDuplicationCaptureStopOnExit(duplication_capture *p) : m_p(p) {}
	~DesktopDuplicationCaptureStopOnExit() {
		m_p->StopCapture();
	}
	void Reset(duplication_capture *p) {
		m_p = p;
	}

private:
	duplication_capture *m_p;
};

class AudioClientStopOnExit {
public:
	AudioClientStopOnExit(IAudioClient *p) : m_p(p) {}
	~AudioClientStopOnExit() {
		HRESULT hr = m_p->Stop();
		if (FAILED(hr)) {
			LOG_ERROR(L"IAudioClient::Stop failed: hr = 0x%08x", hr);
		}
	}

private:
	IAudioClient *m_p;
};

class AvRevertMmThreadCharacteristicsOnExit {
public:
	AvRevertMmThreadCharacteristicsOnExit(HANDLE hTask) : m_hTask(hTask) {}
	~AvRevertMmThreadCharacteristicsOnExit() {
		if (!AvRevertMmThreadCharacteristics(m_hTask)) {
			LOG_ERROR(L"AvRevertMmThreadCharacteristics failed: last error is %d", GetLastError());
		}
	}
private:
	HANDLE m_hTask;
};

class CancelWaitableTimerOnExit {
public:
	CancelWaitableTimerOnExit(HANDLE h) : m_h(h) {}
	~CancelWaitableTimerOnExit() {
		if (!CancelWaitableTimer(m_h)) {
			LOG_ERROR(L"CancelWaitableTimer failed: last error is %d", GetLastError());
		}
	}
private:
	HANDLE m_h;
};

class CloseHandleOnExit {
public:
	CloseHandleOnExit(HANDLE h) : m_h(h) {}
	~CloseHandleOnExit() {
		if (!CloseHandle(m_h)) {
			LOG_ERROR(L"CloseHandle failed: last error is %d", GetLastError());
		}
	}

private:
	HANDLE m_h;
};

class CoTaskMemFreeOnExit {
public:
	CoTaskMemFreeOnExit(PVOID p) : m_p(p) {}
	~CoTaskMemFreeOnExit() {
		CoTaskMemFree(m_p);
	}

private:
	PVOID m_p;
};

class CoUninitializeOnExit {
public:
	~CoUninitializeOnExit() {
		CoUninitialize();
	}
};

class PropVariantClearOnExit {
public:
	PropVariantClearOnExit(PROPVARIANT *p) : m_p(p) {}
	~PropVariantClearOnExit() {
		HRESULT hr = PropVariantClear(m_p);
		if (FAILED(hr)) {
			LOG_ERROR(L"PropVariantClear failed: hr = 0x%08x", hr);
		}
	}

private:
	PROPVARIANT *m_p;
};

class ReleaseOnExit {
public:
	ReleaseOnExit(IUnknown *p) : m_p(p) {}
	~ReleaseOnExit() {
		SafeRelease(&m_p);
	}

private:
	IUnknown *m_p;
};

class DeleteOnExit {
public:
	DeleteOnExit(void *p) : m_p(p) {}
	~DeleteOnExit() {
		delete m_p;
	}

private:
	void *m_p;
};

class SetEventOnExit {
public:
	SetEventOnExit(HANDLE h) : m_h(h) {}
	~SetEventOnExit() {
		if (!SetEvent(m_h)) {
			LOG_ERROR(L"SetEvent failed: last error is %d", GetLastError());
		}
	}
private:
	HANDLE m_h;
};

class WaitForSingleObjectOnExit {
public:
	WaitForSingleObjectOnExit(HANDLE h) : m_h(h) {}
	WaitForSingleObjectOnExit(HANDLE h, DWORD timeoutMillis) : m_h(h), m_millis(timeoutMillis) {}
	~WaitForSingleObjectOnExit() {
		DWORD dwWaitResult = WaitForSingleObject(m_h, m_millis);
		if (WAIT_OBJECT_0 != dwWaitResult) {
			LOG_ERROR(L"WaitForSingleObject returned unexpected result 0x%08x, last error is %d", dwWaitResult, GetLastError());
		}
	}

private:
	HANDLE m_h;
	DWORD m_millis = INFINITE;
};

class ReleaseWWMFSampleDataOnExit {
public:
	ReleaseWWMFSampleDataOnExit(WWMFSampleData *p) : m_p(p) {}
	~ReleaseWWMFSampleDataOnExit() {
		m_p->Release();
	}

private:
	WWMFSampleData *m_p;
};

class ForgetWWMFSampleDataOnExit {
public:
	ForgetWWMFSampleDataOnExit(WWMFSampleData *p) : m_p(p) {}
	~ForgetWWMFSampleDataOnExit() {
		m_p->Forget();
	}

private:
	WWMFSampleData *m_p;
};

class ReleaseDCOnExit {
public:
	ReleaseDCOnExit(HDC p) : m_p(p) {}
	~ReleaseDCOnExit() {
		ReleaseDC(NULL,m_p);
	}

private:
	HDC m_p;
};

class ReleaseKeyedMutexOnExit {
public:
	ReleaseKeyedMutexOnExit(IDXGIKeyedMutex *p, UINT64 key) : m_p(p), m_key(key) {}
	~ReleaseKeyedMutexOnExit() {
	
		if (m_p) {
			m_p->ReleaseSync(m_key);
		}
	}

private:
	IDXGIKeyedMutex *m_p;
	UINT64 m_key;
};
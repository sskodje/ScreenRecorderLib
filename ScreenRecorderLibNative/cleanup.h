// Cleanup.h
#pragma once
#include "Log.h"
#include <vector>
#include <dxgi.h>
//#include <mutex>
template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = nullptr;
	}
}

template<typename Func>
class ExecuteFuncOnExit {
	Func func;
public:
	ExecuteFuncOnExit(Func f) : func(f) {}

	~ExecuteFuncOnExit() {
		func();
	}
};

template<class T>
class ReleaseVectorOnExit {
public:

	ReleaseVectorOnExit(std::vector<T> v)
	{
		_items = v;
	}
	~ReleaseVectorOnExit() {
		for each (T item in _items)
		{
			SafeRelease(&item);
		}
	}
private:
	std::vector<T> _items{};
};

class LeaveCriticalSectionOnExit {
public:
	LeaveCriticalSectionOnExit(CRITICAL_SECTION *p, std::wstring tag = L"") : m_p(p), m_tag(tag) {}
	~LeaveCriticalSectionOnExit() {
		LeaveCriticalSection(m_p);
		if (!m_tag.empty()) {
			//LOG_TRACE("Exited critical section %ls", m_tag.c_str());
		}
	}

private:
	CRITICAL_SECTION *m_p;
	std::wstring m_tag;
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

class DeleteArrayOnExit {
public:
	DeleteArrayOnExit(void *p) : m_p(p) {}
	~DeleteArrayOnExit() {
		delete[] m_p;
	}

private:
	void *m_p;
};

class DeleteGdiObjectOnExit {
public:
	DeleteGdiObjectOnExit(HGDIOBJ p) : m_p(p) {}
	~DeleteGdiObjectOnExit() {
		DeleteObject(m_p);
	}

private:
	HGDIOBJ m_p;
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

class ReleaseDCOnExit {
public:
	ReleaseDCOnExit(HDC p) : m_p(p) {}
	~ReleaseDCOnExit() {
		ReleaseDC(NULL, m_p);
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
			//LOG_TRACE(L"Released keyed mutex with key %d", m_key);
		}
	}

private:
	IDXGIKeyedMutex *m_p;
	UINT64 m_key;
};

class ReleaseMutexHandleOnExit {
public:
	ReleaseMutexHandleOnExit(HANDLE p) : m_p(p) {}
	~ReleaseMutexHandleOnExit() {

		if (m_p) {
			if (!ReleaseMutex(m_p)) {
				LOG_ERROR(L"Failed to release mutex");
			}
		}
	}

private:
	HANDLE m_p;
};

class ReleaseCOMArrayOnExit {
public:
	ReleaseCOMArrayOnExit(IUnknown **array, UINT32 &count) : m_p(array), m_count(count) {}
	~ReleaseCOMArrayOnExit() {

		if (m_p) {
			for (DWORD i = 0; i < m_count; i++)
			{
				SafeRelease(&m_p[i]);
			}
			CoTaskMemFree(m_p);
		}
	}
	void SetCount(int &count) {
		m_count = count;
	}
private:
	IUnknown **m_p;
	UINT32 &m_count;
};
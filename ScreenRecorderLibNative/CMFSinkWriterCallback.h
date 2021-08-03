#pragma once
#include <mfreadwrite.h>

class CMFSinkWriterCallback : public IMFSinkWriterCallback {

public:
	CMFSinkWriterCallback(HANDLE hFinalizeEvent, HANDLE hMarkerEvent) :
		m_nRefCount(0),
		m_hFinalizeEvent(hFinalizeEvent),
		m_hMarkerEvent(hMarkerEvent) {}
	virtual ~CMFSinkWriterCallback()
	{
	}
	// IMFSinkWriterCallback methods
	STDMETHODIMP OnFinalize(HRESULT hrStatus) {
		LOG_DEBUG(L"CMFSinkWriterCallback::OnFinalize");
		if (m_hFinalizeEvent != NULL) {
			SetEvent(m_hFinalizeEvent);
		}
		return hrStatus;
	}

	STDMETHODIMP OnMarker(DWORD dwStreamIndex, LPVOID pvContext) {
		LOG_DEBUG(L"CMFSinkWriterCallback::OnMarker");
		if (m_hMarkerEvent != NULL) {
			SetEvent(m_hMarkerEvent);
		}
		return S_OK;
	}

	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) {
		static const QITAB qit[] = {
			QITABENT(CMFSinkWriterCallback, IMFSinkWriterCallback),
		{0}
		};
		return QISearch(this, qit, riid, ppv);
	}

	STDMETHODIMP_(ULONG) AddRef() {
		return InterlockedIncrement(&m_nRefCount);
	}

	STDMETHODIMP_(ULONG) Release() {
		ULONG refCount = InterlockedDecrement(&m_nRefCount);
		if (refCount == 0) {
			delete this;
		}
		return refCount;
	}

private:
	volatile long m_nRefCount;
	HANDLE m_hFinalizeEvent;
	HANDLE m_hMarkerEvent;
};
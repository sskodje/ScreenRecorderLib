#pragma once
#include <atlbase.h>
#include <atlcom.h>
#include <iostream>
#include <fstream>
#include <MFidl.h>
#include <Mfreadwrite.h>


class ATL_NO_VTABLE WebStream :
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<WebStream>,
	public IStream {
public:
	WebStream();
	/*NOT virtual*/ ~WebStream();

	void SetPortAndWindowHandle(const char *port_str, HWND wnd);

	// Inherited via IStream
	virtual HRESULT STDMETHODCALLTYPE Read(void *pv, ULONG cb, ULONG *pcbRead) override;
	virtual HRESULT STDMETHODCALLTYPE Write(const void *pv, ULONG cb, ULONG *pcbWritten) override;
	virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) override;
	virtual HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize) override;
	virtual HRESULT STDMETHODCALLTYPE CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) override;
	virtual HRESULT STDMETHODCALLTYPE Commit(DWORD grfCommitFlags) override;
	virtual HRESULT STDMETHODCALLTYPE Revert(void) override;
	virtual HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override;
	virtual HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override;
	virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG *pstatstg, DWORD grfStatFlag) override;
	virtual HRESULT STDMETHODCALLTYPE Clone(IStream **ppstm) override;

	BEGIN_COM_MAP(WebStream)
		COM_INTERFACE_ENTRY(IStream)
	END_COM_MAP()

private:
	HRESULT WriteImpl(/*in*/const BYTE *pb, /*in*/ULONG cb);
	CRITICAL_SECTION m_criticalSection;
	unsigned long      m_tmp_bytes_written = 0;

	struct impl;
	std::unique_ptr<impl> m_impl;

};



#define WIN32_LEAN_AND_MEAN
#include <atomic>
#include <Mfapi.h>
#include "WebStream.h"
#include "WebSocket.h"
#include "MP4FragmentEditor.h"
#include <mutex>
#include <thread>

#pragma comment(lib, "mfplat.lib")
struct WebStream::impl : public StreamSockSetter {
	impl(const char *port_str, HWND wnd) : m_server(port_str), m_block_ctor(true), m_wnd(wnd) {

		m_cond_var = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		// start server thread
		m_thread = std::thread(&WebStream::impl::WaitForClients, this);

		// wait for video request or socket failure

		//std::unique_lock<std::mutex> lock(mutex);
		//while (m_block_ctor)
		//    m_cond_var.wait(lock);

		if (WaitForSingleObject(m_cond_var, INFINITE) != WAIT_OBJECT_0) {

		}

		// start streaming video
	}

	void WaitForClients() {
		for (;;) {
			auto current = m_server.WaitForClient();
			if (!current)
				break;

			// create a thread to the handle client connection
			current->Start(m_wnd, this);
			m_clients.push_back(std::move(current));
		}

		Unblock();
	}

	void SetStreamSocket(ClientSock &s) override {
		for (size_t i = 0; i < m_clients.size(); ++i) {
			if (m_clients[i].get() == &s) {
				m_stream_client = std::move(m_clients[i]);
				return;
			}
		}
	}

	void Unblock() override {
		m_block_ctor = false;
		//m_cond_var.notify_all();
		SetEvent(m_cond_var);
	}

	~impl() override {
		// close open sockets (forces blocking calls to complete)
		m_server = ServerSock();
		m_thread.join();

		m_stream_client.reset();

		// wait for client threads to complete
		for (auto &c : m_clients) {
			if (c)
				c.reset();
		}
	}

	ServerSock              m_server;  ///< listens for new connections
	std::unique_ptr<ClientSock> m_stream_client;  ///< video streaming socket
	std::atomic<bool>       m_block_ctor;
	HANDLE                  m_cond_var;
	std::thread             m_thread;
	std::vector<std::unique_ptr<ClientSock>> m_clients; // heap allocated objects to ensure that they never change addreess
	HWND                    m_wnd = nullptr;
	unsigned __int64        m_cur_pos = 0;
	MP4FragmentEditor       m_stream_editor;
};


WebStream::WebStream() {
	InitializeCriticalSection(&m_criticalSection);
}

WebStream::~WebStream() {
}

void WebStream::SetPortAndWindowHandle(const char *port_str, HWND wnd) {
	m_impl = std::make_unique<impl>(port_str, wnd);
}

//HRESULT STDMETHODCALLTYPE WebStream::GetCapabilities(/*out*/DWORD *capabilities) {
//	*capabilities = MFBYTESTREAM_IS_WRITABLE | MFBYTESTREAM_IS_REMOTE;
//	return S_OK;
//}

//HRESULT STDMETHODCALLTYPE WebStream::GetLength(/*out*/QWORD * /*length*/) {
//	return E_NOTIMPL;
//}
//
//HRESULT STDMETHODCALLTYPE WebStream::SetLength(/*in*/QWORD /*length*/) {
//	return E_NOTIMPL;
//}

//HRESULT STDMETHODCALLTYPE WebStream::GetCurrentPosition(/*out*/QWORD *position) {
//	*position = m_impl->m_cur_pos;
//	return S_OK;
//}

//HRESULT STDMETHODCALLTYPE WebStream::SetCurrentPosition(/*in*/QWORD /*position*/) {
//	return E_NOTIMPL;
//}
//
//HRESULT STDMETHODCALLTYPE WebStream::IsEndOfStream(/*out*/BOOL * /*endOfStream*/) {
//	return E_NOTIMPL;
//}
//
//HRESULT STDMETHODCALLTYPE WebStream::Read(/*out*/BYTE * /*pb*/, /*in*/ULONG /*cb*/, /*out*/ULONG * /*bRead*/) {
//	return E_NOTIMPL;
//}
//
//HRESULT STDMETHODCALLTYPE WebStream::BeginRead(/*out*/BYTE * /*pb*/, /*in*/ULONG /*cb*/, /*in*/IMFAsyncCallback * /*callback*/, /*in*/IUnknown * /*unkState*/) {
//	return E_NOTIMPL;
//}
//
//HRESULT STDMETHODCALLTYPE WebStream::EndRead(/*in*/IMFAsyncResult * /*result*/, /*out*/ULONG * /*cbRead*/) {
//	return E_NOTIMPL;
//}

HRESULT WebStream::WriteImpl(/*in*/const BYTE *pb, /*in*/ULONG cb) {
#ifndef ENABLE_FFMPEG
	std::tie(pb, cb) = m_impl->m_stream_editor.EditStream(pb, cb);
#endif

	int byte_count = send(m_impl->m_stream_client->Socket(), reinterpret_cast<const char *>(pb), cb, 0);
	if (byte_count == SOCKET_ERROR) {
		//_com_error error(WSAGetLastError());
		//const TCHAR* msg = error.ErrorMessage();

		// destroy failing client socket (typ. caused by client-side closing)
		m_impl->m_stream_client.reset();
		return E_FAIL;
	}
	m_impl->m_cur_pos += byte_count;
	return S_OK;
}




//HRESULT STDMETHODCALLTYPE WebStream::Write(/*in*/const BYTE *pb, /*in*/ULONG cb, /*out*/ULONG *cbWritten) {
//	EnterCriticalSection(&m_criticalSection);
//	HRESULT hr = WriteImpl(pb, cb);
//	LeaveCriticalSection(&m_criticalSection);
//	if (FAILED(hr))
//		return E_FAIL;
//
//	*cbWritten = cb;
//	return S_OK;
//}

//HRESULT STDMETHODCALLTYPE WebStream::BeginWrite(/*in*/const BYTE *pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback *callback, /*in*/IUnknown *unkState) {
//	EnterCriticalSection(&m_criticalSection);
//
//	HRESULT hr = WriteImpl(pb, cb);
//	if (FAILED(hr)) {
//		LeaveCriticalSection(&m_criticalSection);
//		return E_FAIL;
//	}
//
//	m_tmp_bytes_written = cb;
//
//	CComPtr<IMFAsyncResult> async_res;
//#ifndef ENABLE_FFMPEG
//	if (FAILED(MFCreateAsyncResult(nullptr, callback, unkState, &async_res)))
//		throw std::runtime_error("MFCreateAsyncResult failed");
//#endif
//
//	hr = callback->Invoke(async_res); // will trigger EndWrite
//	return hr;
//}

//HRESULT STDMETHODCALLTYPE WebStream::EndWrite(/*in*/IMFAsyncResult * /*result*/, /*out*/ULONG *cbWritten) {
//	*cbWritten = m_tmp_bytes_written;
//	m_tmp_bytes_written = 0;
//
//	LeaveCriticalSection(&m_criticalSection);
//	return S_OK;
//}
//
//HRESULT STDMETHODCALLTYPE WebStream::Seek(/*in*/MFBYTESTREAM_SEEK_ORIGIN /*SeekOrigin*/, /*in*/LONGLONG /*SeekOffset*/,/*in*/DWORD /*SeekFlags*/, /*out*/QWORD * /*CurrentPosition*/) {
//	return E_NOTIMPL;
//}
//
//HRESULT STDMETHODCALLTYPE WebStream::Flush() {
//	return S_OK;
//}
//
//HRESULT STDMETHODCALLTYPE WebStream::Close() {
//	EnterCriticalSection(&m_criticalSection);
//
//	m_impl.reset();
//
//	LeaveCriticalSection(&m_criticalSection);
//	return S_OK;
//}


HRESULT STDMETHODCALLTYPE WebStream::Read(void *pv, ULONG cb, ULONG *pcbRead) {
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WebStream::Write(const void *pv, ULONG cb, ULONG *pcbWritten) {
	EnterCriticalSection(&m_criticalSection);
	HRESULT hr = WriteImpl(static_cast<const BYTE *>(pv), cb);
	LeaveCriticalSection(&m_criticalSection);
	if (FAILED(hr))
		return E_FAIL;

	*pcbWritten = cb;
	return S_OK;
}
HRESULT STDMETHODCALLTYPE WebStream::Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) {
	plibNewPosition->QuadPart = dlibMove.QuadPart;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WebStream::SetSize(ULARGE_INTEGER /*libNewSize*/) {
	return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE WebStream::CopyTo(IStream */*pstm*/, ULARGE_INTEGER /*cb*/, ULARGE_INTEGER */*pcbRead*/, ULARGE_INTEGER */*pcbWritten*/) {
	return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE WebStream::Commit(DWORD /*grfCommitFlags*/) {
	return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE WebStream::Revert(void) {
	return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE WebStream::LockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/, DWORD /*dwLockType*/) {
	return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE WebStream::UnlockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/, DWORD /*dwLockType*/) {
	return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE WebStream::Stat(STATSTG */*pstatstg*/, DWORD /*grfStatFlag*/) {
	return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE WebStream::Clone(IStream **/*ppstm*/) {
	return E_NOTIMPL;
}
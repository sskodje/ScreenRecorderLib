#pragma once
#include <Windows.h>
#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

namespace ScreenRecorderLib {
	private class ManagedIStream : public IStream
	{
	public:
		ManagedIStream(System::IO::Stream ^ baseStream)
			: refCount(1)
		{
			this->baseStream = baseStream;
			tempBytes = gcnew array<byte, 1>(4194304);
		}

	public:
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject)
		{
			if (riid == __uuidof (IStream)) { *ppvObject = this; AddRef(); return S_OK; }
			else { *ppvObject = NULL; return E_NOINTERFACE; }
		}
		virtual ULONG STDMETHODCALLTYPE AddRef(void) { return InterlockedIncrement(&refCount); }
		virtual ULONG STDMETHODCALLTYPE Release(void)
		{
			ULONG temp = InterlockedDecrement(&refCount);
			if (temp == 0) delete this;
			return temp;
		}

	public:
		// IStream
		virtual HRESULT STDMETHODCALLTYPE Read(void *pv, _In_  ULONG cb, _Out_opt_ ULONG *pcbRead)override
		{
			if (!baseStream->CanRead)
				return E_ACCESSDENIED;
			if (((int)cb) >= tempBytes->Length) {
				tempBytes = gcnew array<byte, 1>(cb + 1);
			}
			int bytesRead = baseStream->Read(tempBytes, 0, cb);
			System::Runtime::InteropServices::Marshal::Copy(tempBytes, 0, System::IntPtr(pv), bytesRead);

			if (pcbRead != nullptr) *pcbRead = bytesRead;
			return S_OK;
		}
		virtual HRESULT STDMETHODCALLTYPE Write(const void *pv, ULONG cb, ULONG *pcbWritten) override
		{
			if (!baseStream->CanWrite)
				return E_ACCESSDENIED;
			if (((int)cb) >= tempBytes->Length) {
				tempBytes = gcnew array<byte, 1>(cb + 1);
			}
			System::Runtime::InteropServices::Marshal::Copy(System::IntPtr((void *)pv), tempBytes, 0, cb);
			baseStream->Write(tempBytes, 0, cb);
			if (pcbWritten != nullptr) *pcbWritten = cb;
			return S_OK;
		}
		virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, _Out_opt_  ULARGE_INTEGER *plibNewPosition) override
		{
			if (!baseStream->CanSeek)
				return E_ACCESSDENIED;
			System::IO::SeekOrigin seekOrigin;

			switch (dwOrigin)
			{
			case 0: seekOrigin = System::IO::SeekOrigin::Begin; break;
			case 1: seekOrigin = System::IO::SeekOrigin::Current; break;
			case 2: seekOrigin = System::IO::SeekOrigin::End; break;
			default: throw gcnew System::ArgumentOutOfRangeException("dwOrigin");
			}

			long long position = baseStream->Seek(dlibMove.QuadPart, seekOrigin);
			if (plibNewPosition != nullptr)
				plibNewPosition->QuadPart = position;
			return S_OK;
		}
		virtual HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize)
		{
			baseStream->SetLength(libNewSize.QuadPart);
			return S_OK;
		}
		virtual HRESULT STDMETHODCALLTYPE CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) override
		{
			return E_NOTIMPL;
		}
		virtual HRESULT STDMETHODCALLTYPE Commit(DWORD  grfCommitFlags) override { return E_NOTIMPL; }
		virtual HRESULT STDMETHODCALLTYPE Revert(void) override { return E_NOTIMPL; }
		virtual HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override { return E_NOTIMPL; }
		virtual HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override { return E_NOTIMPL; }

		virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG * pstatstg, DWORD grfStatFlag) override
		{
			memset(pstatstg, 0, sizeof(STATSTG));
			pstatstg->type = STGTY_STREAM;
			pstatstg->cbSize.QuadPart = baseStream->Length;

			if (baseStream->CanRead && baseStream->CanWrite)
				pstatstg->grfMode |= STGM_READWRITE;
			else if (baseStream->CanRead)
				pstatstg->grfMode |= STGM_READ;
			else if (baseStream->CanWrite)
				pstatstg->grfMode |= STGM_WRITE;
			else throw gcnew System::IO::IOException();
			return S_OK;
		}
		virtual HRESULT STDMETHODCALLTYPE Clone(IStream **ppstm) override { return E_NOTIMPL; }

	private:
		ULONG refCount;
		gcroot<System::IO::Stream^> baseStream;
		gcroot<array<byte, 1>^> tempBytes;
	};
}

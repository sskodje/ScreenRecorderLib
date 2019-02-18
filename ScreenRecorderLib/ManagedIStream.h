#pragma once
#include <Windows.h>
#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>
#include "ManagedStreamWrapper.h"

namespace ScreenRecorderLib {
	private class ManagedIStream : public IStream
	{
	public:
		ManagedIStream(System::IO::Stream^ stream)
			: refCount(1)
		{
			this->baseStream = gcnew ManagedStreamWrapper(stream);

		   /* https://stackoverflow.com/a/39712297/2129964
			* We need to call all functions on the managed stream through delegates,
			* or we get errors if the code is running in a non-default AppDomain.*/
			m_pSeekFnc = baseStream->GetSeekFunctionPointer();
			m_pWriteFnc = baseStream->GetWriteFunctionPointer();
			m_pReadFnc = baseStream->GetReadFunctionPointer();
			m_pCanWriteFnc = baseStream->GetCanWriteFunctionPointer();
			m_pCanReadFnc = baseStream->GetCanReadFunctionPointer();
			m_pCanSeekFnc = baseStream->GetCanSeekFunctionPointer();
			m_pSetLengthFnc = baseStream->GetSetLengthFunctionPointer();
			m_pGetLengthFnc = baseStream->GetLengthFunctionPointer();
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
			if (!m_pCanReadFnc())
				return E_ACCESSDENIED;

			int bytesRead = m_pReadFnc(System::IntPtr(pv), 0, cb);
			if (pcbRead != nullptr) *pcbRead = bytesRead;
			return S_OK;
		}
		virtual HRESULT STDMETHODCALLTYPE Write(const void *pv, ULONG cb, ULONG *pcbWritten) override
		{
			if (!m_pCanWriteFnc())
				return E_ACCESSDENIED;

			m_pWriteFnc(System::IntPtr((void *)pv), 0, cb);
			if (pcbWritten != nullptr) *pcbWritten = cb;
			return S_OK;
		}
		virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, _Out_opt_  ULARGE_INTEGER *plibNewPosition) override
		{
			if (!m_pCanSeekFnc())
				return E_ACCESSDENIED;
			System::IO::SeekOrigin seekOrigin;

			switch (dwOrigin)
			{
			case 0: seekOrigin = System::IO::SeekOrigin::Begin; break;
			case 1: seekOrigin = System::IO::SeekOrigin::Current; break;
			case 2: seekOrigin = System::IO::SeekOrigin::End; break;
			default: throw gcnew System::ArgumentOutOfRangeException("dwOrigin");
			}

			long long position = m_pSeekFnc(dlibMove.QuadPart, seekOrigin);
			if (plibNewPosition != nullptr)
				plibNewPosition->QuadPart = position;
			return S_OK;
		}
		virtual HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize)
		{
			m_pSetLengthFnc(libNewSize.QuadPart);
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

		virtual HRESULT STDMETHODCALLTYPE Stat(::STATSTG * pstatstg, DWORD grfStatFlag) override
		{
			memset(pstatstg, 0, sizeof(::STATSTG));
			pstatstg->type = STGTY_STREAM;
			pstatstg->cbSize.QuadPart = m_pGetLengthFnc();

			if (m_pCanReadFnc() && m_pCanWriteFnc())
				pstatstg->grfMode |= STGM_READWRITE;
			else if (m_pCanReadFnc())
				pstatstg->grfMode |= STGM_READ;
			else if (m_pCanWriteFnc())
				pstatstg->grfMode |= STGM_WRITE;
			else throw gcnew System::IO::IOException();
			return S_OK;
		}
		virtual HRESULT STDMETHODCALLTYPE Clone(IStream **ppstm) override { return E_NOTIMPL; }

	private:
		ULONG refCount;
		gcroot<ManagedStreamWrapper^> baseStream;

		SeekFnc *m_pSeekFnc;
		ReadFnc *m_pReadFnc;
		WriteFnc *m_pWriteFnc;
		CanReadFnc *m_pCanReadFnc;
		CanWriteFnc *m_pCanWriteFnc;
		CanSeekFnc *m_pCanSeekFnc;
		SetLengthFnc *m_pSetLengthFnc;
		GetLengthFnc *m_pGetLengthFnc;
	};
}

#pragma once
typedef bool(__stdcall CanReadFnc)();
typedef bool(__stdcall CanWriteFnc)();
typedef bool(__stdcall CanSeekFnc)();
typedef long long(__stdcall SeekFnc)(long long offset, System::IO::SeekOrigin origin);
typedef int(__stdcall ReadFnc)(System::IntPtr dest, int offset, int count);
typedef void(__stdcall WriteFnc)(System::IntPtr, int offset, int count);
typedef void(__stdcall SetLengthFnc)(long long value);
typedef long long(__stdcall GetLengthFnc)();

namespace ScreenRecorderLib {
	public ref class ManagedStreamWrapper
	{
	public:
		ManagedStreamWrapper(System::IO::Stream ^stream);


		SeekFnc *GetSeekFunctionPointer()
		{
			return   (SeekFnc*)(System::Runtime::InteropServices::Marshal::GetFunctionPointerForDelegate(m_SeekDelegate).ToPointer());
		}

		WriteFnc *GetWriteFunctionPointer()
		{
			return   (WriteFnc*)(System::Runtime::InteropServices::Marshal::GetFunctionPointerForDelegate(m_WriteDelegate).ToPointer());
		}

		ReadFnc *GetReadFunctionPointer()
		{
			return   (ReadFnc*)(System::Runtime::InteropServices::Marshal::GetFunctionPointerForDelegate(m_ReadDelegate).ToPointer());
		}

		SetLengthFnc *GetSetLengthFunctionPointer()
		{
			return   (SetLengthFnc*)(System::Runtime::InteropServices::Marshal::GetFunctionPointerForDelegate(m_SetLengthDelegate).ToPointer());
		}

		GetLengthFnc *GetLengthFunctionPointer()
		{
			return   (GetLengthFnc*)(System::Runtime::InteropServices::Marshal::GetFunctionPointerForDelegate(m_GetLengthDelegate).ToPointer());
		}
		CanReadFnc *GetCanReadFunctionPointer()
		{
			return   (CanReadFnc*)(System::Runtime::InteropServices::Marshal::GetFunctionPointerForDelegate(m_CanReadDelegate).ToPointer());
		}
		CanWriteFnc *GetCanWriteFunctionPointer()
		{
			return   (CanWriteFnc*)(System::Runtime::InteropServices::Marshal::GetFunctionPointerForDelegate(m_CanWriteDelegate).ToPointer());
		}
		CanSeekFnc *GetCanSeekFunctionPointer()
		{
			return   (CanSeekFnc*)(System::Runtime::InteropServices::Marshal::GetFunctionPointerForDelegate(m_CanSeekDelegate).ToPointer());
		}


	private:
		System::IO::Stream ^m_Stream;
		delegate long long LongDelegate();
		delegate long long SeekDelegate(long long offset, System::IO::SeekOrigin origin);
		delegate int ReadDelegate(System::IntPtr dest, int offset, int count);
		delegate void WriteDelegate(System::IntPtr source, int offset, int count);
		delegate void SetLengthDelegate(long long value);
		delegate int IntDelegate();
		delegate bool BoolDelegate();
		delegate array<byte, 1>^ BufferDelegate();

		LongDelegate ^m_GetLengthDelegate;
		BoolDelegate ^m_CanWriteDelegate;
		BoolDelegate ^m_CanReadDelegate;
		BoolDelegate ^m_CanSeekDelegate;
		SeekDelegate ^m_SeekDelegate;
		ReadDelegate ^m_ReadDelegate;
		WriteDelegate ^m_WriteDelegate;
		SetLengthDelegate ^m_SetLengthDelegate;

		long long GetLength() {
			return m_Stream->Length;
		}
		bool GetCanWrite() {
			return m_Stream->CanWrite;
		}
		bool GetCanRead() {
			return m_Stream->CanRead;
		}
		bool GetCanSeek() {
			return m_Stream->CanSeek;
		}
		long long Seek(long long offset, System::IO::SeekOrigin origin) {
			return m_Stream->Seek(offset, origin);
		};
		int Read(System::IntPtr dest, int offset, int count) {
			auto tempBytes = gcnew array<byte, 1>(count);
			int bytesRead = m_Stream->Read(tempBytes, offset, count);
			System::Runtime::InteropServices::Marshal::Copy(tempBytes, 0, dest, count);
			return bytesRead;
		};
		void Write(System::IntPtr source, int offset, int count) {
			auto tempBytes = gcnew array<byte, 1>(count);
			System::Runtime::InteropServices::Marshal::Copy(source, tempBytes, 0, count);
			return m_Stream->Write(tempBytes, offset, count);
		};
		void SetLength(long long value) override {
			return m_Stream->SetLength(value);
		};
	};


	ManagedStreamWrapper::ManagedStreamWrapper(System::IO::Stream ^stream)
	{
		m_Stream = stream;
		m_GetLengthDelegate = gcnew LongDelegate(this, &ManagedStreamWrapper::GetLength);
		m_CanWriteDelegate = gcnew BoolDelegate(this, &ManagedStreamWrapper::GetCanWrite);
		m_CanReadDelegate = gcnew BoolDelegate(this, &ManagedStreamWrapper::GetCanRead);
		m_CanSeekDelegate = gcnew BoolDelegate(this, &ManagedStreamWrapper::GetCanSeek);
		m_SeekDelegate = gcnew SeekDelegate(this, &ManagedStreamWrapper::Seek);
		m_ReadDelegate = gcnew ReadDelegate(this, &ManagedStreamWrapper::Read);
		m_WriteDelegate = gcnew WriteDelegate(this, &ManagedStreamWrapper::Write);
		m_SetLengthDelegate = gcnew SetLengthDelegate(this, &ManagedStreamWrapper::SetLength);
	}
}
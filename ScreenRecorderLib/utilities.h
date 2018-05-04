#pragma once
#include <winnt.h>
#include <locale>
#include <codecvt>
#include "log.h"
#define RETURN_ON_BAD_HR(expr) \
{ \
    HRESULT _hr_ = (expr); \
    if (FAILED(_hr_)) { \
	{\
		_com_error err(_hr_);\
		ERR(L"RETURN_ON_BAD_HR: %ls", err.ErrorMessage());\
	}\
		return _hr_; \
	} \
}

class utilities
{
public:
	static std::wstring s2ws(const std::string& s)
	{
		int len;
		int slength = (int)s.length() + 1;
		len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
		wchar_t* buf = new wchar_t[len];
		MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
		std::wstring r(buf);
		delete[] buf;
		return r;
	}
	static std::string ws2s(const std::wstring& wstr)
	{
		using convert_typeX = std::codecvt_utf8<wchar_t>;
		std::wstring_convert<convert_typeX, wchar_t> converterX;

		return converterX.to_bytes(wstr);
	}
	static bool  CreateAllDirs(std::wstring path) {
		int pos = 0;
		do
		{
			pos = path.find_first_of(L"\\/", pos + 1);
			BOOL result = CreateDirectory(path.substr(0, pos).c_str(), NULL);
			if (!result) {
				DWORD error = GetLastError();
				if (ERROR_ALREADY_EXISTS != error){
					return false;
				}
			}
		} while (pos != std::string::npos);
		return true;
	}
	// Create a string with last error message
	static std::wstring GetLastErrorStdWstr() {
		return s2ws(GetLastErrorStdStr());
	}
	// Create a string with last error message
	static std::string GetLastErrorStdStr()
	{
		DWORD error = GetLastError();
		if (error)
		{
			LPVOID lpMsgBuf;
			DWORD bufLen = FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				error,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR)&lpMsgBuf,
				0, NULL);
			if (bufLen)
			{
				LPCSTR lpMsgStr = (LPCSTR)lpMsgBuf;
				std::string result(lpMsgStr, lpMsgStr + bufLen);

				LocalFree(lpMsgBuf);

				return result;
			}
		}
		return std::string();
	}
};


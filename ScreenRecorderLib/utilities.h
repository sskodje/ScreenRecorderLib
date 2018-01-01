#pragma once
#include <winnt.h>
#include <locale>
#include <codecvt>

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
};


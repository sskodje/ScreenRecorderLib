#pragma once
#include <winnt.h>
#include <locale>
#include <comdef.h>
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

#define LOG_ON_BAD_HR(expr) \
{ \
    HRESULT _hr_ = (expr); \
    if (FAILED(_hr_)) { \
    {\
        _com_error err(_hr_);\
        ERR(L"BAD HR: %ls", err.ErrorMessage());\
    }\
    } \
}
class utilities
{
public:
    static std::wstring s2ws(const std::string& str)
    {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }
    static std::string ws2s(const std::wstring& wstr)
    {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string r(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &r[0], size_needed, NULL, NULL);
        return r;
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
                nullptr,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPTSTR)&lpMsgBuf,
                0, nullptr);
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


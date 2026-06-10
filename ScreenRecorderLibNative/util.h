#pragma warning (disable : 26451)
#pragma once
#include <Windows.h>
#include <locale>
#include <comdef.h>
#include "log.h"
#include <string>
#include <chrono>
#include <algorithm>

template < class T, class U >
bool isinst(U u) {
	return dynamic_cast<T>(u) != nullptr;
}

template<typename ... Args>
std::wstring string_format(const std::wstring &format, Args ... args)
{
	size_t size = swprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
	if (size <= 0) { throw std::runtime_error("Error during formatting."); }
	std::unique_ptr<wchar_t[]> buf(new wchar_t[size]);
	swprintf(buf.get(), size, format.c_str(), args ...);
	return std::wstring(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

#define RETURN_RESULT_ON_BAD_HR(hr,errorText) \
{ \
    HRESULT _hr_ = (hr); \
	std::wstring _errorText_ = (errorText);\
    if (FAILED(_hr_)) { \
    {\
        _com_error err(_hr_);\
        LOG_ERROR(L"RETURN_RESULT_ON_BAD_HR: hr=0x%08x, error is: %ls", _hr_, err.ErrorMessage());\
    }\
	REC_RESULT captureResult{};\
	captureResult.RecordingResult = _hr_;\
	captureResult.Error = _errorText_;\
    return captureResult; \
    } \
}

#define RETURN_ON_BAD_HR(expr) \
{ \
    HRESULT _hr_ = (expr); \
    if (FAILED(_hr_)) { \
    {\
        _com_error err(_hr_);\
        LOG_ERROR(L"RETURN_ON_BAD_HR: hr=0x%08x, error is: %ls", _hr_, err.ErrorMessage());\
    }\
        return _hr_; \
    } \
}

#define CONTINUE_ON_BAD_HR(expr) \
{ \
    HRESULT _hr_ = (expr); \
    if (FAILED(_hr_)) { \
    {\
        _com_error err(_hr_);\
        LOG_ERROR(L"CONTINUE_ON_BAD_HR: hr=0x%08x, error is: %ls", _hr_, err.ErrorMessage());\
    }\
       continue; \
    } \
}

#define BREAK_ON_BAD_HR(expr) \
{ \
    HRESULT _hr_ = (expr); \
    if (FAILED(_hr_)) { \
    {\
        _com_error err(_hr_);\
        LOG_ERROR(L"BREAK_ON_BAD_HR: hr=0x%08x, error is: %ls", _hr_, err.ErrorMessage());\
    }\
       break; \
    } \
}

#define LOG_ON_BAD_HR(expr) \
{ \
    HRESULT _hr_ = (expr); \
    if (FAILED(_hr_)) { \
    {\
        _com_error err(_hr_);\
        LOG_ERROR(L"BAD HR: hr=0x%08x, error is: %ls", _hr_, err.ErrorMessage());\
    }\
    } \
}

std::wstring s2ws(const std::string &str);

std::string ws2s(const std::wstring &wstr);

// Create a string with last error message
std::string GetLastErrorStdStr();

// Create a string with last error message
inline std::wstring GetLastErrorStdWstr() {
	return s2ws(GetLastErrorStdStr());
}

inline INT64 MillisToHundredNanos(double millis) {
	return (INT64)round(millis * 10 * 1000);
}

inline INT64 SecondsToHundredNanos(double seconds) {
	return (INT64)round(seconds * 10 * 1000 * 1000);
}

inline double HundredNanosToMillisDouble(INT64 hundredNanos) {
	return (double)hundredNanos / 10 / 1000;
}

inline INT64 HundredNanosToMillis(INT64 hundredNanos) {
	return hundredNanos / 10 / 1000;
}
inline double HundredNanosToSeconds(INT64 hundredNanos) {
	return (double)hundredNanos / 10 / 1000 / 1000;
}
/// <summary>
/// Forces the dimensions of rect to be even by adding 1*modifier pixel if odd.
/// </summary>
inline RECT MakeRectEven(_In_ RECT &rect, _In_ int modifier = -1)
{
	if ((rect.right - rect.left) % 2 != 0)
		rect.right += 1 * modifier;
	if ((rect.bottom - rect.top) % 2 != 0)
		rect.bottom += 1 * modifier;
	return rect;
}
/// <summary>
/// Forces the dimensions of n to be even by adding 1*modifier pixel if odd.
/// </summary>
inline LONG MakeEven(_In_ LONG n, _In_ int modifier = -1) {
	return n + (1 * modifier) * n % 2;
}

inline LONG RectWidth(RECT rc)
{
	return rc.right - rc.left;
}

inline LONG RectHeight(RECT rc)
{
	return rc.bottom - rc.top;
}

inline bool IsValidRect(RECT rc) {
	return rc.right > rc.left && rc.bottom > rc.top;
}

enum class ImageFileType
{
	IMAGE_FILE_JPG,      // joint photographic experts group - .jpeg or .jpg
	IMAGE_FILE_PNG,      // portable network graphics
	IMAGE_FILE_GIF,      // graphics interchange format 
	IMAGE_FILE_TIFF,     // tagged image file format
	IMAGE_FILE_BMP,      // Microsoft bitmap format
	IMAGE_FILE_WEBP,     // Google WebP format, a type of .riff file
	IMAGE_FILE_ICO,      // Microsoft icon format
	IMAGE_FILE_INVALID,  // unidentified image types.
};
ImageFileType getImageTypeByMagic(const char *data);

std::string ReadFileSignature(std::wstring filePath);

std::string ReadFileSignature(IStream *pStream);

bool IsFileAvailableForReading(std::wstring filePath);

std::string CurrentTimeToFormattedString(bool withMilliseconds);

UINT GetSystemDpi();

bool TryParseDWORD(const std::wstring &input, DWORD &result);

std::wstring GetProcessNameFromPID(DWORD processID);
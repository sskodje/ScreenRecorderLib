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

inline std::wstring s2ws(const std::string& str)
{
	if (str.empty()) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
	return wstrTo;
}
inline std::string ws2s(const std::wstring& wstr)
{
	if (wstr.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string r(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &r[0], size_needed, NULL, NULL);
	return r;
}


// Create a string with last error message
inline std::string GetLastErrorStdStr()
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

// Create a string with last error message
inline std::wstring GetLastErrorStdWstr() {
	return s2ws(GetLastErrorStdStr());
}

inline INT64 MillisToHundredNanos(INT64 millis) {
	return millis * 10 * 1000;
}

inline INT64 HundredNanosToMillis(INT64 hundredNanos) {
	return (INT64)round((double)hundredNanos / 10 / 1000);
}

inline double HundredNanosToMillisDouble(INT64 hundredNanos) {
	return (double)hundredNanos / 10 / 1000;
}
/// <summary>
/// This method forces the dimensions of a RECT to be even by subtracting one pixel if odd.
/// </summary>
/// <param name="rect"></param>
inline RECT MakeRectEven(_In_ RECT &rect)
{
	if ((rect.right - rect.left) % 2 != 0)
		rect.right -= 1;
	if ((rect.bottom - rect.top) % 2 != 0)
		rect.bottom -= 1;
	return rect;
}

inline LONG RectWidth(RECT rc)
{
	return rc.right - rc.left;
}

inline LONG RectHeight(RECT rc)
{
	return rc.bottom - rc.top;
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
inline ImageFileType getImageTypeByMagic(const char *data)
{
	//if (len < 16) return IMAGE_FILE_INVALID;

	// .jpg:  FF D8 FF
	// .png:  89 50 4E 47 0D 0A 1A 0A
	// .gif:  GIF87a      
	//        GIF89a
	// .tiff: 49 49 2A 00
	//        4D 4D 00 2A
	// .bmp:  BM 
	// .webp: RIFF ???? WEBP 
	// .ico   00 00 01 00
	//        00 00 02 00 ( cursor files )

	switch (data[0])
	{
	case '\xFF':
		return (!strncmp((const char*)data, "\xFF\xD8\xFF", 3)) ?
			ImageFileType::IMAGE_FILE_JPG : ImageFileType::IMAGE_FILE_INVALID;

	case '\x89':
		return (!strncmp((const char*)data,
			"\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8)) ?
			ImageFileType::IMAGE_FILE_PNG : ImageFileType::IMAGE_FILE_INVALID;

	case 'G':
		return (!strncmp((const char*)data, "GIF87a", 6) ||
			!strncmp((const char*)data, "GIF89a", 6)) ?
			ImageFileType::IMAGE_FILE_GIF : ImageFileType::IMAGE_FILE_INVALID;

	case 'I':
		return (!strncmp((const char*)data, "\x49\x49\x2A\x00", 4)) ?
			ImageFileType::IMAGE_FILE_TIFF : ImageFileType::IMAGE_FILE_INVALID;

	case 'M':
		return (!strncmp((const char*)data, "\x4D\x4D\x00\x2A", 4)) ?
			ImageFileType::IMAGE_FILE_TIFF : ImageFileType::IMAGE_FILE_INVALID;

	case 'B':
		return ((data[1] == 'M')) ?
			ImageFileType::IMAGE_FILE_BMP : ImageFileType::IMAGE_FILE_INVALID;

	case 'R':
		if (strncmp((const char*)data, "RIFF", 4))
			return ImageFileType::IMAGE_FILE_INVALID;
		if (strncmp((const char*)(data + 8), "WEBP", 4))
			return ImageFileType::IMAGE_FILE_INVALID;
		return ImageFileType::IMAGE_FILE_WEBP;

	case '\0':
		if (!strncmp((const char*)data, "\x00\x00\x01\x00", 4))
			return ImageFileType::IMAGE_FILE_ICO;
		if (!strncmp((const char*)data, "\x00\x00\x02\x00", 4))
			return ImageFileType::IMAGE_FILE_ICO;
		return ImageFileType::IMAGE_FILE_INVALID;

	default:
		return ImageFileType::IMAGE_FILE_INVALID;
	}
}

inline std::string ReadFileSignature(std::wstring filePath) {
	char buffer[16]; // Buffer to store data
	FILE * stream;
	_wfopen_s(&stream, filePath.c_str(), L"r");
	size_t count = fread(&buffer, sizeof(char), 16, stream);
	fclose(stream);
	auto string = std::string(buffer);
	return string;
}
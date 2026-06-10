#include "util.h"

using _GetDpiForSystem = UINT __stdcall();

/// <summary>
/// Returns the system DPI from GetDpiForSystem() on Windows 10 v1607 or above, 
/// or falls back to using GetDeviceCaps for earlier windows versions.
/// </summary>
/// <returns></returns>
UINT GetSystemDpi()
{
	auto libraryModule = LoadLibraryA("User32.dll");
	HRESULT hr = E_FAIL;
	int dpi = 96;
	if (libraryModule != nullptr)
	{
		auto addr = GetProcAddress(libraryModule, "GetDpiForSystem");
		if (addr != nullptr)
		{
			auto GetDpi = reinterpret_cast<_GetDpiForSystem *>(addr);
			dpi = GetDpi();
		}
		else {
			auto dc = GetDC(nullptr);
			dpi = GetDeviceCaps(dc, LOGPIXELSX);
			ReleaseDC(nullptr, dc);
		}
		FreeLibrary(libraryModule);
	}

	return dpi;
}

std::wstring s2ws(const std::string &str)
{
	if (str.empty()) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
	return wstrTo;
}

std::string ws2s(const std::wstring &wstr)
{
	if (wstr.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string r(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &r[0], size_needed, NULL, NULL);
	return r;
}
// Create a string with last error message
std::string GetLastErrorStdStr()
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

ImageFileType getImageTypeByMagic(const char *data)
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
			return (!strncmp((const char *)data, "\xFF\xD8\xFF", 3)) ?
				ImageFileType::IMAGE_FILE_JPG : ImageFileType::IMAGE_FILE_INVALID;

		case '\x89':
			return (!strncmp((const char *)data,
				"\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8)) ?
				ImageFileType::IMAGE_FILE_PNG : ImageFileType::IMAGE_FILE_INVALID;

		case 'G':
			return (!strncmp((const char *)data, "GIF87a", 6) ||
				!strncmp((const char *)data, "GIF89a", 6)) ?
				ImageFileType::IMAGE_FILE_GIF : ImageFileType::IMAGE_FILE_INVALID;

		case 'I':
			return (!strncmp((const char *)data, "\x49\x49\x2A\x00", 4)) ?
				ImageFileType::IMAGE_FILE_TIFF : ImageFileType::IMAGE_FILE_INVALID;

		case 'M':
			return (!strncmp((const char *)data, "\x4D\x4D\x00\x2A", 4)) ?
				ImageFileType::IMAGE_FILE_TIFF : ImageFileType::IMAGE_FILE_INVALID;

		case 'B':
			return ((data[1] == 'M')) ?
				ImageFileType::IMAGE_FILE_BMP : ImageFileType::IMAGE_FILE_INVALID;

		case 'R':
			if (strncmp((const char *)data, "RIFF", 4))
				return ImageFileType::IMAGE_FILE_INVALID;
			if (strncmp((const char *)(data + 8), "WEBP", 4))
				return ImageFileType::IMAGE_FILE_INVALID;
			return ImageFileType::IMAGE_FILE_WEBP;

		case '\0':
			if (!strncmp((const char *)data, "\x00\x00\x01\x00", 4))
				return ImageFileType::IMAGE_FILE_ICO;
			if (!strncmp((const char *)data, "\x00\x00\x02\x00", 4))
				return ImageFileType::IMAGE_FILE_ICO;
			return ImageFileType::IMAGE_FILE_INVALID;

		default:
			return ImageFileType::IMAGE_FILE_INVALID;
	}
}

std::string ReadFileSignature(std::wstring filePath) {
	FILE *stream;
	std::string signature = "";
	_wfopen_s(&stream, filePath.c_str(), L"r");
	if (stream) {
		char buffer[16]{ 0 }; // Buffer to store data
		int charNum = 16;
		size_t count = fread(&buffer, sizeof(char), charNum, stream);
		if (count == charNum) {
			signature = std::string(buffer);
		}
		fclose(stream);
	}
	return signature;
}

std::string ReadFileSignature(IStream *pStream) {
	std::string signature = "";
	if (pStream) {
		LARGE_INTEGER li = { 0 };
		HRESULT hr = pStream->Seek(li, STREAM_SEEK_SET, 0);
		if (FAILED(hr)) {
			LOG_ERROR("Could not seek in source stream while reading source signature");
			return "";
		}
		char buffer[16]{ 0 }; // Buffer to store data
		int charNum = 16;
		ULONG count;
		hr = pStream->Read(&buffer, charNum, &count);
		if (FAILED(hr)) {
			LOG_ERROR("Could not read from source stream while reading source signature");
			return "";
		}
		if (count == charNum) {
			signature = std::string(buffer);
		}
		pStream->Seek(li, STREAM_SEEK_SET, 0);
	}
	return signature;
}

bool IsFileAvailableForReading(std::wstring filePath) {
	FILE *stream;
	_wfopen_s(&stream, filePath.c_str(), L"r");
	if (stream) {
		fclose(stream);
		return true;
	}
	return false;
}

std::string CurrentTimeToFormattedString(bool withMilliseconds = false) {
	SYSTEMTIME systemTime;
	GetSystemTime(&systemTime);

	FILETIME fileTime;
	SystemTimeToFileTime(&systemTime, &fileTime);

	// Convert the FILETIME to 100-nanosecond intervals since January 1, 1601
	ULARGE_INTEGER largeInteger;
	largeInteger.LowPart = fileTime.dwLowDateTime;
	largeInteger.HighPart = fileTime.dwHighDateTime;
	unsigned long long fileTime100ns = largeInteger.QuadPart;

	// Convert 100-nanosecond intervals to milliseconds (divide by 10,000)
	unsigned long long milliseconds = fileTime100ns / 10000;

	// Format the time as a string with three decimal places for milliseconds
	std::ostringstream timeStream;
	timeStream << std::setfill('0') << std::setw(2) << systemTime.wHour << "-"
		<< std::setfill('0') << std::setw(2) << systemTime.wMinute << "-"
		<< std::setfill('0') << std::setw(2) << systemTime.wSecond << ".";
	if (withMilliseconds) {
		timeStream << std::setfill('0') << std::setw(3) << (milliseconds % 1000);
	}
	return timeStream.str();
}

bool TryParseDWORD(const std::wstring &input, DWORD &result)
{
	if (input.empty())
		return false;

	try
	{
		size_t pos = 0;
		unsigned long value = std::stoul(input, &pos, 10);

		// Ensure entire string was parsed
		if (pos != input.length())
			return false;

		result = static_cast<DWORD>(value);
		return true;
	}
	catch (const std::exception &)
	{
		return false; // invalid format or overflow
	}
}

std::wstring GetProcessNameFromPID(DWORD processID) {
	std::wstring processName = L"<unknown>";

	// 1. Open the process with limited query privileges
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processID);

	if (hProcess != NULL) {
		wchar_t buffer[MAX_PATH];
		DWORD size = MAX_PATH;

		// 2. Retrieve the full executable path
		if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size)) {
			std::wstring fullPath(buffer);

			// 3. Extract just the executable name from the path
			size_t lastSlash = fullPath.find_last_of(L"\\");
			if (lastSlash != std::wstring::npos) {
				processName = fullPath.substr(lastSlash + 1);
			}
			else {
				processName = fullPath;
			}
		}

		// 4. Always close handles to prevent resource leaks
		CloseHandle(hProcess);
	}

	return processName;
}
#pragma once
#define LOG(format, ...) _log(format L"\n", __VA_ARGS__)
#define ERR(format, ...) LOG(L"Error: " format, __VA_ARGS__)
inline  void _log(const wchar_t * format, ...) {
//#if _DEBUG
	wchar_t buffer[256];
	va_list args;
	va_start(args, format);
	vswprintf(buffer, 256, format, args);
	OutputDebugStringW(buffer);
	va_end(args);
//#endif
}
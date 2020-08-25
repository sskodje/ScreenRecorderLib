#pragma once
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

#define LOG_LVL_TRACE 0
#define LOG_LVL_DEBUG 1
#define LOG_LVL_INFO 2
#define LOG_LVL_WARN 3
#define LOG_LVL_ERR 4

#define TRACE(format, ...) _log(LOG_LVL_TRACE, L"%s [TRACE] [%hs(%hs:%d)] >> " format L"\n", getTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__)
#define DEBUG(format, ...) _log(LOG_LVL_DEBUG, L"%s [DEBUG] [%hs(%hs:%d)] >> " format L"\n", getTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__)
#define INFO(format, ...) _log(LOG_LVL_INFO,L"%s [INFO]  [%hs(%hs:%d)] >> " format L"\n", getTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__)
#define WARN(format, ...) _log(LOG_LVL_ERR,L"%s [WARN]  [%hs(%hs:%d)] >> " format L"\n", getTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__)
#define ERROR(format, ...) _log(LOG_LVL_ERR,L"%s [ERROR] [%hs(%hs:%d)] >> " format L"\n", getTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__)

extern bool isLoggingEnabled;
extern int logSeverityLevel;
extern std::wstring logFilePath;

inline  void _log(int logLvl, const wchar_t * format, ...) {
	if (isLoggingEnabled && logLvl >= logSeverityLevel) {
		wchar_t buffer[256];
		va_list args;
		va_start(args, format);
		vswprintf(buffer, 256, format, args);
		if (!logFilePath.empty()) {

			std::wofstream logFile(logFilePath, std::ios_base::app | std::ios_base::out);
			if (logFile.is_open())
			{
				logFile << buffer;
				logFile.close();
			}
		}
		else {
			OutputDebugStringW(buffer);
		}
		va_end(args);
	}
}

constexpr const char* file_name(const char* path) {
	const char* file = path;
	while (*path) {
		if (*path++ == '\\') {
			file = path;
		}
	}
	return file;
}

static std::wstring getTimestamp() {
	// get a precise timestamp as a string
	const auto now = std::chrono::system_clock::now();
	const auto nowAsTimeT = std::chrono::system_clock::to_time_t(now);
	const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()) % 1000;
	std::wstringstream nowSs;
	nowSs
		<< std::put_time(std::localtime(&nowAsTimeT), L"%Y-%m-%d %H:%M:%S")
		<< '.' << std::setfill(L'0') << std::setw(3) << nowMs.count();
	return nowSs.str();
}
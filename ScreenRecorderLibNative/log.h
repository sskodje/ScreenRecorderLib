#pragma once
#include <Windows.h>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

#if _DEBUG
#define MEASURE_EXECUTION_TIME false
#else
#define MEASURE_EXECUTION_TIME false
#endif

#define LOG_BUFFER_SIZE 1024

#define LOG_LVL_TRACE 0
#define LOG_LVL_DEBUG 1
#define LOG_LVL_INFO 2
#define LOG_LVL_WARN 3
#define LOG_LVL_ERR 4

#define LOG_TRACE(format, ...) if(isLoggingEnabled && LOG_LVL_TRACE >= logSeverityLevel) {_log(L"%s [TRACE] [%hs(%hs:%d)] >> " format L"\n", GetTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__);}
#define LOG_DEBUG(format, ...) if(isLoggingEnabled && LOG_LVL_DEBUG >= logSeverityLevel) {_log(L"%s [DEBUG] [%hs(%hs:%d)] >> " format L"\n", GetTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__);}
#define LOG_INFO(format, ...) if(isLoggingEnabled && LOG_LVL_INFO >= logSeverityLevel) {_log(L"%s [INFO]  [%hs(%hs:%d)] >> " format L"\n", GetTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__);}
#define LOG_WARN(format, ...) if(isLoggingEnabled && LOG_LVL_WARN >= logSeverityLevel) {_log(L"%s [WARN]  [%hs(%hs:%d)] >> " format L"\n", GetTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__);}
#define LOG_ERROR(format, ...) if(isLoggingEnabled && LOG_LVL_ERR >= logSeverityLevel) {_log(L"%s [ERROR] [%hs(%hs:%d)] >> " format L"\n", GetTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__);}

extern bool isLoggingEnabled;
extern int logSeverityLevel;
extern std::wstring logFilePath;
void _log(PCWSTR format, ...);
std::wstring GetTimestamp();

constexpr const char *file_name(const char *path) {
	const char *file = path;
	while (*path) {
		if (*path++ == '\\') {
			file = path;
		}
	}
	return file;
}

struct MeasureExecutionTime {
private:
	std::wstring m_Name;
	std::chrono::steady_clock::time_point m_Start;
public:
	MeasureExecutionTime(std::wstring name) {
		if (MEASURE_EXECUTION_TIME == true) {
			m_Name = name;
			m_Start = std::chrono::steady_clock::now();
		}
	}
	~MeasureExecutionTime() {
		if (MEASURE_EXECUTION_TIME == true) {
			std::chrono::duration<double, std::milli> ms_double = std::chrono::steady_clock::now() - m_Start;
			if (!m_Name.empty()) {
				LOG_TRACE("Execution time for %ls: %.2f ms", m_Name.c_str(), ms_double.count());
			}
		}
	}
	void SetName(std::wstring name) {
		m_Name = name;
	}
};
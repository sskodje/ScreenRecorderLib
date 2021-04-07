#pragma once
#include <Windows.h>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

#define MEASURE_EXECUTION_TIME true

#define LOG_BUFFER_SIZE 1024

#define LOG_LVL_TRACE 0
#define LOG_LVL_DEBUG 1
#define LOG_LVL_INFO 2
#define LOG_LVL_WARN 3
#define LOG_LVL_ERR 4

#define LOG_TRACE(format, ...) _log(LOG_LVL_TRACE, L"%s [TRACE] [%hs(%hs:%d)] >> " format L"\n", getTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(format, ...) _log(LOG_LVL_DEBUG, L"%s [DEBUG] [%hs(%hs:%d)] >> " format L"\n", getTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__)
#define LOG_INFO(format, ...) _log(LOG_LVL_INFO,L"%s [INFO]  [%hs(%hs:%d)] >> " format L"\n", getTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__)
#define LOG_WARN(format, ...) _log(LOG_LVL_ERR,L"%s [WARN]  [%hs(%hs:%d)] >> " format L"\n", getTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(format, ...) _log(LOG_LVL_ERR,L"%s [ERROR] [%hs(%hs:%d)] >> " format L"\n", getTimestamp().c_str(), file_name(__FILE__), __func__, __LINE__, __VA_ARGS__)

extern bool isLoggingEnabled;
extern int logSeverityLevel;
extern std::wstring logFilePath;
void _log(int logLvl, PCWSTR format, ...);

constexpr const char *file_name(const char *path) {
	const char *file = path;
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
	tm localTime;
	localtime_s(&localTime, &nowAsTimeT);
	const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
	std::wstringstream nowSs;
	nowSs
		<< std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S")
		<< '.' << std::setfill(L'0') << std::setw(3) << nowMs.count();
	return nowSs.str();
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
			LOG_TRACE("Execution time for %ls: %.2f ms", m_Name.c_str(), ms_double.count());
		}
	}
	void SetName(std::wstring name) {
		m_Name = name;
	}
};
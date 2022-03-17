#include "Log.h"
#include <mutex>

static std::mutex g_Mtx{};

void _log(PCWSTR format, ...)
{
	g_Mtx.lock();
	wchar_t buffer[LOG_BUFFER_SIZE];
	va_list args;
	va_start(args, format);

	vswprintf_s(buffer, LOG_BUFFER_SIZE, format, args);
	if (!logFilePath.empty()) {

		std::wofstream logFile(logFilePath, std::ios_base::app | std::ios_base::out);
		if (logFile.is_open())
		{
			logFile << buffer;
			logFile.close();
		}
		else {
			OutputDebugStringW(L"Error opening log file for write");
		}
	}
	else {
		OutputDebugStringW(buffer);
	}
	va_end(args);
	g_Mtx.unlock();

}

std::wstring GetTimestamp() {
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

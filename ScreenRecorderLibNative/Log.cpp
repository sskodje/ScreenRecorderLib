#include "Log.h"
#include <mutex>

static std::mutex g_Mtx{};

void _log(int logLvl, PCWSTR format, ...)
{
	if (isLoggingEnabled && logLvl >= logSeverityLevel) {
		g_Mtx.lock();
		wchar_t buffer[LOG_BUFFER_SIZE];
		va_list args;
		va_start(args, format);
		vswprintf(buffer, LOG_BUFFER_SIZE, format, args);
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
}

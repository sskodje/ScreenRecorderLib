#pragma once
#include <dxgi.h>
// WinRT
#include <winrt/base.h>


struct MonitorInfo
{
	MonitorInfo(HMONITOR monitorHandle)
	{
		MonitorHandle = monitorHandle;
		MONITORINFOEX monitorInfo = { sizeof(monitorInfo) };
		winrt::check_bool(GetMonitorInfo(MonitorHandle, &monitorInfo));
		std::wstring displayName(monitorInfo.szDevice);
		DisplayName = displayName;
		MonitorRect = monitorInfo.rcMonitor;
		WorkspaceRect = monitorInfo.rcWork;
	}
	MonitorInfo(HMONITOR monitorHandle, std::wstring const& displayName)
	{
		MonitorHandle = monitorHandle;
		DisplayName = displayName;
	}

	HMONITOR MonitorHandle;
	std::wstring DisplayName;
	RECT MonitorRect;
	RECT WorkspaceRect;

	bool operator==(const MonitorInfo& monitor) { return MonitorHandle == monitor.MonitorHandle; }
	bool operator!=(const MonitorInfo& monitor) { return !(*this == monitor); }
};

class monitor_list
{
public:
	monitor_list(bool includeAllMonitors);

	const std::vector<MonitorInfo> GetCurrentMonitors() { return m_monitors; }

	const std::optional<MonitorInfo> GetMonitorForDisplayName(std::wstring name) {
		for (MonitorInfo info : m_monitors)
		{
			if (info.DisplayName == name) {
				return std::make_optional(info);
			}
		}
		return std::nullopt;
	}
private:
	std::vector<MonitorInfo> m_monitors;
};
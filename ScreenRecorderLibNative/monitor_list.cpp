#include "monitor_list.h"

std::vector<MonitorInfo> EnumerateAllMonitors(bool includeAllMonitors)
{
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam)
    {
        auto& monitors = *reinterpret_cast<std::vector<MonitorInfo>*>(lparam);
        monitors.push_back(MonitorInfo(hmon));

        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    if (monitors.size() > 1 && includeAllMonitors)
    {
        monitors.push_back(MonitorInfo(nullptr, L"All Displays"));
    }
    return monitors;
}

monitor_list::monitor_list(bool includeAllMonitors)
{
    m_monitors = EnumerateAllMonitors(includeAllMonitors);
}


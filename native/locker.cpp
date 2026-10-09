// 鼠标锁定核心实现。
#include "locker.h"

#include <vector>
#include <algorithm>

namespace
{
    BOOL CALLBACK EnumMonitorsProc(HMONITOR hMon, HDC, LPRECT, LPARAM data)
    {
        auto* list = reinterpret_cast<std::vector<DisplayInfo>*>(data);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, &mi))
        {
            std::wstring name = mi.szDevice;
            auto pos = name.find(L"DISPLAY");
            if (pos != std::wstring::npos) name = L"屏幕 " + name.substr(pos + 7);
            name += L"   ·   " + std::to_wstring(mi.rcMonitor.right-mi.rcMonitor.left) + L" × " + std::to_wstring(mi.rcMonitor.bottom-mi.rcMonitor.top);
            if (mi.dwFlags & MONITORINFOF_PRIMARY) name += L"   主屏幕";
            list->push_back({hMon, mi.rcMonitor, mi.szDevice, name});
        }
        return TRUE;
    }

}
    std::vector<DisplayInfo> GetDisplays()
    {
        std::vector<DisplayInfo> list;
        EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, reinterpret_cast<LPARAM>(&list));
        std::sort(list.begin(), list.end(), [](const DisplayInfo& a, const DisplayInfo& b) {
            if (a.rect.left != b.rect.left)
                return a.rect.left < b.rect.left;
            return a.rect.top < b.rect.top;
        });
        return list;
    }

bool Locker::LockToCurrentScreen()
{
    POINT pt{};
    if (!GetCursorPos(&pt))
        return false;
    return LockToScreenAt(pt.x, pt.y);
}

bool Locker::LockToScreenAt(int x, int y)
{
    POINT pt{ x, y };
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    return LockToMonitor(hMon);
}

bool Locker::LockToMonitor(HMONITOR hMon)
{
    if (!hMon)
        return false;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi))
        return false;

    RECT rect = mi.rcMonitor;
    if (!ClipCursor(&rect))
        return false;

    m_lockedRect = rect;
    m_locked = true;
    return true;
}

void Locker::Unlock()
{
    if (m_locked) ClipCursor(nullptr);
    m_locked = false;
}

bool Locker::Jump(int direction)
{
    auto monitors = GetDisplays();
    if (monitors.size() < 2)
        return false;

    POINT pt{};
    if (!GetCursorPos(&pt))
        return false;
    HMONITOR hCur = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    int idx = -1;
    for (size_t i = 0; i < monitors.size(); ++i)
        if (monitors[i].handle == hCur) { idx = (int)i; break; }
    if (idx < 0)
        return false;

    int n = (int)monitors.size();
    int target = (idx + direction) % n;
    if (target < 0)
        target += n;

    const RECT& r = monitors[target].rect;
    int cx = r.left + (r.right - r.left) / 2;
    int cy = r.top + (r.bottom - r.top) / 2;
    ClipCursor(nullptr);
    SetCursorPos(cx, cy);

    return LockToScreenAt(cx, cy);
}

void Locker::Revalidate()
{
    if (!m_locked)
        return;

    POINT pt{};
    if (!GetCursorPos(&pt))
        return;

    const RECT& r = m_lockedRect;
    bool inside = pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom;
    RECT actual{};
    if (!GetClipCursor(&actual) || !EqualRect(&actual, &m_lockedRect)) ClipCursor(&m_lockedRect);
    if (inside) return;

    int x = pt.x < r.left ? r.left : (pt.x > r.right - 1 ? r.right - 1 : pt.x);
    int y = pt.y < r.top ? r.top : (pt.y > r.bottom - 1 ? r.bottom - 1 : pt.y);
    SetCursorPos(x, y);
    RECT rect = r;
    ClipCursor(&rect);
}

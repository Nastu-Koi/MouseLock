#include "notifications.h"
#include <algorithm>

namespace
{
    constexpr UINT DismissTimer = 1;
    constexpr int Width = 396, Height = 124;
    constexpr const wchar_t* ClassName = L"MouseLockGame.Notification.v2";
}

bool NotificationPopup::Show(HWND owner, const std::wstring& title, const std::wstring& detail,
                             HMONITOR monitor, UINT durationMs)
{
    monitor_ = monitor;
    title_ = title;
    detail_ = detail;
    if (!hwnd_)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = ClassName;
        RegisterClassW(&wc);
        // The window is deliberately absent from Alt+Tab, cannot take focus and
        // lets pointer input reach the game or desktop beneath it.
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                                  WS_EX_TRANSPARENT | WS_EX_LAYERED,
            ClassName, title.c_str(), WS_POPUP, 0, 0, 1, 1,
            owner, nullptr, wc.hInstance, this);
        if (!hwnd_) return false;
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    }
    KillTimer(hwnd_, DismissTimer);
    SetWindowTextW(hwnd_, title.c_str());
    Position();
    if (!SetTimer(hwnd_, DismissTimer, durationMs, nullptr))
    { ShowWindow(hwnd_, SW_HIDE); return false; }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    ++revision_;
    NotifyWinEvent(EVENT_SYSTEM_ALERT, hwnd_, OBJID_WINDOW, CHILDID_SELF);
    return true;
}

void NotificationPopup::Position()
{
    if (positioning_ || !hwnd_) return;
    positioning_ = true;
    MONITORINFO info{sizeof(info), {}, {}, 0};
    if (!monitor_ || !GetMonitorInfoW(monitor_, &info))
    {
        POINT cursor{}; GetCursorPos(&cursor);
        monitor_ = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        GetMonitorInfoW(monitor_, &info);
    }
    // Keep the whole popup inside its destination while Windows resolves DPI.
    // Probing at the lower-right edge can put it mostly on a neighboring monitor.
    UINT dpi = GetDpiForWindow(hwnd_);
    if (dpi && theme_.dpi != dpi) theme_.SetDpi(dpi);
    for (int pass = 0; pass < 2; ++pass)
    {
        UINT layoutDpi = theme_.dpi;
        int width = theme_.S(Width), height = theme_.S(Height), gap = theme_.S(20);
        int x = std::max(static_cast<int>(info.rcWork.left), static_cast<int>(info.rcWork.right)-width-gap);
        int y = std::max(static_cast<int>(info.rcWork.top), static_cast<int>(info.rcWork.bottom)-height-gap);
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
        dpi = GetDpiForWindow(hwnd_);
        if (dpi && theme_.dpi != dpi) theme_.SetDpi(dpi);
        if (theme_.dpi == layoutDpi) break;
    }
    RECT bounds{}; GetClientRect(hwnd_, &bounds);
    auto region = CreateRoundRectRgn(0, 0, bounds.right+1, bounds.bottom+1, theme_.S(28), theme_.S(28));
    if (!SetWindowRgn(hwnd_, region, TRUE)) DeleteObject(region);
    positioning_ = false;
}

void NotificationPopup::Paint()
{
    Ui::Paint paint(hwnd_, theme_);
    auto dc = paint.dc;
    Ui::Round(dc, theme_.R(0, 0, Width, Height), Ui::Background, Ui::Border, theme_.S(14));
    Ui::Round(dc, theme_.R(20, 20, 7, 7), Ui::Accent, Ui::Accent, theme_.S(3));
    Ui::Label(dc, theme_, L"MOUSELOCK", 36, 12, Width-56, 24, theme_.caption, Ui::Muted);
    Ui::Label(dc, theme_, title_, 20, 39, Width-40, 31, theme_.heading, Ui::Text);
    auto old = SelectObject(dc, theme_.body);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, Ui::Muted);
    RECT detailRect = theme_.R(20, 78, Width-40, 40);
    DrawTextW(dc, detail_.c_str(), -1, &detailRect, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

void NotificationPopup::Close()
{
    if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

LRESULT CALLBACK NotificationPopup::WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    auto* self = reinterpret_cast<NotificationPopup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = static_cast<NotificationPopup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wp, lp);
    switch (message)
    {
    case WM_PAINT: self->Paint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_DPICHANGED:
        self->theme_.SetDpi(HIWORD(wp));
        self->Position();
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_TIMER:
        if (wp == DismissTimer) { KillTimer(hwnd, DismissTimer); ShowWindow(hwnd, SW_HIDE); }
        return 0;
    case WM_DISPLAYCHANGE: self->Position(); return 0;
    case WM_NCDESTROY:
        KillTimer(hwnd, DismissTimer); self->hwnd_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); break;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

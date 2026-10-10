#include "theme.h"
#include "gdiplus_compat.h"
#include <dwmapi.h>
#include <commctrl.h>
#include <algorithm>

namespace Ui
{
    namespace
    {
        HFONT Font(UINT dpi, int size, int weight, const wchar_t* face)
        {
            return CreateFontW(-MulDiv(size, dpi, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
        }
        Gdiplus::Color Color(COLORREF c)
        {
            return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
        }
        LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR, DWORD_PTR)
        {
            if (msg == WM_MOUSEMOVE && !GetPropW(hwnd, L"UiHover"))
            {
                SetPropW(hwnd, L"UiHover", reinterpret_cast<HANDLE>(1));
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (msg == WM_MOUSELEAVE)
            {
                RemovePropW(hwnd, L"UiHover");
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (msg == WM_NCDESTROY)
            {
                RemovePropW(hwnd, L"UiHover");
                RemovePropW(hwnd, L"UiPrimary");
                RemovePropW(hwnd, L"UiOnPanel");
                RemoveWindowSubclass(hwnd, ButtonProc, 1);
            }
            return DefSubclassProc(hwnd, msg, wp, lp);
        }
    }

    Theme::Theme()
    {
        background = CreateSolidBrush(Background);
        panel = CreateSolidBrush(Panel);
        SetDpi(96);
    }
    Theme::~Theme()
    {
        for (auto font : {body, caption, heading, display, key}) DeleteObject(font);
        DeleteObject(background);
        DeleteObject(panel);
    }
    void Theme::SetDpi(UINT value)
    {
        dpi = value;
        for (auto font : {body, caption, heading, display, key}) if (font) DeleteObject(font);
        body = Font(dpi, 14, FW_NORMAL, L"Microsoft YaHei UI");
        caption = Font(dpi, 12, FW_NORMAL, L"Microsoft YaHei UI");
        heading = Font(dpi, 21, FW_MEDIUM, L"Microsoft YaHei UI");
        display = Font(dpi, 36, FW_NORMAL, L"Georgia");
        key = Font(dpi, 14, FW_MEDIUM, L"Segoe UI");
    }

    void Layout(const Theme& theme, const std::vector<Placement>& controls)
    {
        for (const auto& c : controls)
        {
            SetWindowPos(c.hwnd, nullptr, theme.S(c.x), theme.S(c.y), theme.S(c.w), theme.S(c.h),
                         SWP_NOZORDER | SWP_NOACTIVATE);
            SendMessageW(c.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(theme.body), TRUE);
        }
    }
    HWND Button(HWND parent, int id, const wchar_t* text, bool primary)
    {
        HWND hwnd = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (primary) SetPropW(hwnd, L"UiPrimary", reinterpret_cast<HANDLE>(1));
        SetWindowSubclass(hwnd, ButtonProc, 1, 0);
        return hwnd;
    }
    void Round(HDC dc, RECT r, COLORREF fill, COLORREF stroke, int radius)
    {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath p;
        float x = static_cast<float>(r.left) + .5f, y = static_cast<float>(r.top) + .5f;
        float w = static_cast<float>(r.right-r.left)-1, h = static_cast<float>(r.bottom-r.top)-1;
        float d = static_cast<float>(std::min(radius*2, static_cast<int>(h)));
        if (w <= 0 || h <= 0 || d <= 0) return;
        p.AddArc(x, y, d, d, 180, 90);
        p.AddArc(x+w-d, y, d, d, 270, 90);
        p.AddArc(x+w-d, y+h-d, d, d, 0, 90);
        p.AddArc(x, y+h-d, d, d, 90, 90);
        p.CloseFigure();
        Gdiplus::SolidBrush brush(Color(fill));
        Gdiplus::Pen pen(Color(stroke));
        g.FillPath(&brush, &p);
        g.DrawPath(&pen, &p);
    }
    void Label(HDC dc, const Theme& t, const std::wstring& text, int x, int y, int w, int h,
               HFONT font, COLORREF color, UINT align)
    {
        auto previous = SelectObject(dc, font ? font : t.body);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        RECT r = t.R(x, y, w, h);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r,
                  align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, previous);
    }
    void Card(HDC dc, const Theme& t, int x, int y, int w, int h)
    {
        Round(dc, t.R(x,y,w,h), Panel, Border, t.S(16));
    }
    void Keycap(HDC dc, const Theme& t, const std::wstring& keyText, int x, int y, int w)
    {
        Round(dc, t.R(x,y,w,32), Background, Border, t.S(7));
        Label(dc,t,keyText,x+6,y,w-12,32,t.key,Text,DT_CENTER);
    }
    void Line(HDC dc, const Theme& t, int x, int y, int w)
    {
        auto pen = CreatePen(PS_SOLID, 1, Border);
        auto old = SelectObject(dc, pen);
        MoveToEx(dc, t.S(x), t.S(y), nullptr);
        LineTo(dc, t.S(x+w), t.S(y));
        SelectObject(dc, old); DeleteObject(pen);
    }
    void DrawButton(const Theme& t, const DRAWITEMSTRUCT& item)
    {
        bool primary = GetPropW(item.hwndItem,L"UiPrimary") != nullptr;
        bool hover = GetPropW(item.hwndItem,L"UiHover") != nullptr;
        bool pressed = (item.itemState & ODS_SELECTED) != 0;
        bool disabled = (item.itemState & ODS_DISABLED) != 0;
        FillRect(item.hDC, &item.rcItem, GetPropW(item.hwndItem,L"UiOnPanel") ? t.panel : t.background);
        COLORREF fill = primary ? Accent : Panel;
        if (hover || pressed) fill = primary ? RGB(154,70,43) : AccentSoft;
        if (disabled) fill = Background;
        Round(item.hDC, item.rcItem, fill, primary ? fill : Border, t.S(9));
        wchar_t text[256]{}; GetWindowTextW(item.hwndItem,text,256);
        auto old = SelectObject(item.hDC,t.body);
        SetBkMode(item.hDC,TRANSPARENT);
        SetTextColor(item.hDC,disabled ? Muted : primary ? RGB(255,255,255) : Text);
        RECT r=item.rcItem; InflateRect(&r,-t.S(8),0);
        DrawTextW(item.hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        if (item.itemState & ODS_FOCUS)
        {
            r=item.rcItem; InflateRect(&r,-t.S(4),-t.S(4));
            DrawFocusRect(item.hDC,&r);
        }
        SelectObject(item.hDC,old);
    }
    void WindowChrome(HWND hwnd)
    {
        COLORREF background=Background, foreground=Text;
        DwmSetWindowAttribute(hwnd,35,&background,sizeof(background));
        DwmSetWindowAttribute(hwnd,36,&foreground,sizeof(foreground));
        DWORD round=2; DwmSetWindowAttribute(hwnd,33,&round,sizeof(round));
    }
    bool SaveSnapshot(HWND hwnd, const std::wstring& path)
    {
        RECT r{}; GetWindowRect(hwnd,&r);
        HDC screen=GetDC(hwnd), memory=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,r.right-r.left,r.bottom-r.top);
        auto old=SelectObject(memory,bitmap);
        BOOL captured=PrintWindow(hwnd,memory,2);
        SelectObject(memory,old);
        bool ok=false;
        if (captured)
        {
            Gdiplus::Bitmap image(bitmap,nullptr);
            CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            ok=image.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok;
        }
        DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(hwnd,screen);
        return ok;
    }
    Paint::Paint(HWND hwnd, const Theme& theme): hwnd_(hwnd)
    {
        target_=BeginPaint(hwnd,&ps_);
        GetClientRect(hwnd,&rect_);
        dc=CreateCompatibleDC(target_);
        bitmap_=CreateCompatibleBitmap(target_,std::max(1L,rect_.right),std::max(1L,rect_.bottom));
        previous_=SelectObject(dc,bitmap_);
        FillRect(dc,&rect_,theme.background);
    }
    Paint::~Paint()
    {
        BitBlt(target_,0,0,rect_.right,rect_.bottom,dc,0,0,SRCCOPY);
        SelectObject(dc,previous_); DeleteObject(bitmap_); DeleteDC(dc);
        EndPaint(hwnd_,&ps_);
    }
}

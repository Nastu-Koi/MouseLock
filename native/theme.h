#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace Ui
{
    constexpr COLORREF Background = RGB(250, 249, 245);
    constexpr COLORREF Panel = RGB(255, 255, 253);
    constexpr COLORREF Text = RGB(48, 46, 42);
    constexpr COLORREF Muted = RGB(113, 109, 100);
    constexpr COLORREF Border = RGB(226, 222, 212);
    constexpr COLORREF Accent = RGB(177, 84, 53);
    constexpr COLORREF AccentSoft = RGB(245, 232, 222);
    constexpr COLORREF Green = RGB(83, 108, 79);

    struct Theme
    {
        UINT dpi = 96;
        HFONT body = nullptr, small = nullptr, heading = nullptr, display = nullptr, key = nullptr;
        HBRUSH background = nullptr, panel = nullptr;
        Theme();
        ~Theme();
        void SetDpi(UINT value);
        int S(int v) const { return MulDiv(v, dpi, 96); }
        RECT R(int x, int y, int w, int h) const { return {S(x), S(y), S(x+w), S(y+h)}; }
    };

    struct Placement { HWND hwnd; int x, y, w, h; };
    void Layout(const Theme& theme, const std::vector<Placement>& controls);
    HWND Button(HWND parent, int id, const wchar_t* text, bool primary = false);
    void DrawButton(const Theme& theme, const DRAWITEMSTRUCT& item);
    void WindowChrome(HWND hwnd);
    void Round(HDC dc, RECT rect, COLORREF fill, COLORREF stroke, int radius);
    void Label(HDC dc, const Theme& theme, const std::wstring& text, int x, int y, int w, int h,
               HFONT font = nullptr, COLORREF color = Text, UINT align = DT_LEFT);
    void Card(HDC dc, const Theme& theme, int x, int y, int w, int h);
    void Keycap(HDC dc, const Theme& theme, const std::wstring& key, int x, int y, int w);
    void Line(HDC dc, const Theme& theme, int x, int y, int w);
    bool SaveSnapshot(HWND hwnd, const std::wstring& path);

    class Paint
    {
    public:
        Paint(HWND hwnd, const Theme& theme);
        ~Paint();
        HDC dc;
    private:
        HWND hwnd_;
        PAINTSTRUCT ps_{};
        HDC target_;
        HBITMAP bitmap_;
        HGDIOBJ previous_;
        RECT rect_{};
    };
}

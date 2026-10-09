#pragma once
#include "theme.h"

// One non-activating popup is reused. New events replace old text instead of
// queuing stale messages during rapid toggles or game focus changes.
class NotificationPopup
{
public:
    bool Show(HWND owner, const std::wstring& title, const std::wstring& detail,
              HMONITOR monitor, UINT durationMs = 2400);
    void Close();
    HWND Window() const { return hwnd_; }
    unsigned Revision() const { return revision_; }
private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
    void Position();
    void Paint();
    HWND hwnd_ = nullptr;
    HMONITOR monitor_ = nullptr;
    Ui::Theme theme_;
    std::wstring title_, detail_;
    unsigned revision_ = 0;
    bool positioning_ = false;
};

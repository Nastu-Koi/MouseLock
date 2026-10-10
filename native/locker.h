// 鼠标锁定核心：ClipCursor 锁定/解锁、屏幕跳转、备用校验。
#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct DisplayInfo
{
    HMONITOR handle;
    RECT rect;
    std::wstring device;
    std::wstring label;
};
std::vector<DisplayInfo> GetDisplays();

// 按设备名查找屏幕；不生成显示名称，供定时校验使用。未连接时返回 nullptr。
HMONITOR FindDisplay(const std::wstring& device);

class Locker
{
public:
    bool IsLocked() const { return m_locked; }

    // 锁定到包含指定物理坐标的屏幕
    bool LockToScreenAt(int x, int y);
    bool LockToMonitor(HMONITOR monitor);

    // 解除锁定
    void Unlock();

    // 跳转到相邻屏幕（-1 上一个，+1 下一个）
    bool Jump(int direction);

    // 备用校验：光标越界时夹回并重新裁剪
    void Revalidate();

private:
    bool m_locked = false;
    RECT m_lockedRect{};
};

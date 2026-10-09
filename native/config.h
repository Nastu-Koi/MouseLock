// 配置模块：配置结构、手写 JSON 读写、快捷键格式化。
#pragma once

#include <string>
#include <vector>

// 修饰键（与旧 C# 版 HoldModifier 的字符串枚举保持一致）
enum class Modifier
{
    Control = 0,
    Alt = 1,
    Shift = 2
};

// 一个快捷键：若干修饰键 + 一个主键（VK 码，0 表示仅修饰键）
// key == -1 表示“未设置”（对应旧版 null）
struct Hotkey
{
    std::vector<Modifier> modifiers;
    int key = 0; // 0 = 无主键；-1 = 未设置
};

// 是否已设置（未设置时不参与匹配与格式化）
inline bool IsSet(const Hotkey& hk) { return hk.key != -1; }

// 应用配置（对应旧 config.json 结构）
struct AppConfig
{
    Hotkey unlock{{}, 0x79}; // F10：长按临时解锁
    Hotkey jumpLeft{{Modifier::Control, Modifier::Alt}, 0x25};
    Hotkey jumpRight{{Modifier::Control, Modifier::Alt}, 0x27};
    Hotkey toggle{{}, 0x7B}; // F12
    Hotkey exit{{}, 0x78};   // F9
    unsigned exitHoldMs = 2000;
    bool enabled = false;
    bool gameOnly = false;
    std::wstring monitor; // 空：启用时光标所在屏幕；游戏模式跟随前台游戏
    std::wstring processes = L"VALORANT-Win64-Shipping.exe;VALORANT.exe";
    bool startSilent = false;
    bool autoStart = false;
};

// 从 JSON 文本加载配置；失败或缺失时回退默认值。
AppConfig LoadConfig(const std::wstring& path);

// 将配置写回 JSON 文本（格式上与旧版可互相读取）。
bool SaveConfig(const std::wstring& path, const AppConfig& cfg);

// 导入用户现有 MouseLockGame 1.x 的平面 INI；不改动旧文件。
AppConfig ImportLegacyConfig(const std::wstring& path);
bool ValidateConfig(const AppConfig& cfg, std::wstring& error);
std::vector<std::wstring> ProcessNames(const std::wstring& text);

// 把快捷键格式化为可读文本（如 "Ctrl + Alt + ←"）。
std::wstring FormatHotkey(const Hotkey& hk);

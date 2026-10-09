// 设置对话框：快捷键、启动选项与开机自启。
#pragma once

#include <windows.h>
#include <functional>
#include "config.h"

using ApplySettings = std::function<bool(const AppConfig&, std::wstring&)>;
void ShowSettingsDialog(HWND parent, const AppConfig& current, const ApplySettings& onChanged,
                        const std::function<void(bool)>& onCapture,
                        const std::wstring& snapshotDirectory = L"");
bool SettingsIsRecording();
bool SettingsIsOpen();
void CloseSettingsForExit();

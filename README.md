# MouseLock Game

基于 [bmoqing/MouseLock](https://github.com/bmoqing/MouseLock) 的原生 Win32 鼠标锁定工具。本版本补齐现有 MouseLockGame 游戏版的主要功能，增加全局长按退出，并采用 Claude 风格的暖白背景、陶土橙按钮、柔和圆角和简洁排版。

## 使用

运行 `MouseLockGame.exe`。选择「偏好设置 → 锁定与启动」可以调整工作模式、目标屏幕、游戏进程和启动选项。

| 操作 | 默认按键 |
| --- | --- |
| 启用 / 暂停锁定 | F12 |
| 按住临时解锁，松开回到原屏幕 | F10 |
| **长按退出程序并释放鼠标** | **F9，连续按住 2 秒** |
| 跳转上一 / 下一屏幕 | Ctrl + Alt + ← / → |
| 紧急解除锁定（保留，不能覆盖） | Ctrl + Alt + F12 |

退出按键与时长可在「偏好设置 → 快捷键」修改。时长可选 1–10 秒。短按不会退出，松开会取消计时，多次短按不累计；键盘自动重复不会重置计时。收起窗口、停用鼠标锁定时，长按退出仍然有效。录入快捷键时暂停退出检测，录入后必须先松开按键。

点击快捷键按钮开始录入，Esc 取消，Backspace 清空。「重置」恢复单项默认值，「恢复默认」恢复所有设置并停用锁定。快捷键、屏幕和启动选项自动保存；修改进程列表后点击「保存列表」。

关闭窗口只会收起到托盘。单击 / 双击托盘图标可显示窗口；右键托盘可暂停锁定、切换屏幕、打开设置或退出。也可使用主窗口中的「退出程序」。

## 操作通知（2.1）

启动程序、再次打开已运行的程序、启用或暂停锁定、临时解锁与恢复、切换工作模式或目标屏幕、游戏前后台变化、紧急解除以及退出时，屏幕右下角会出现暖色通知。

普通提醒约 2.4 秒后自动消失，不抢焦点、不拦截鼠标点击。连续操作会替换同一个提醒，不会排队弹出旧状态；后台定时检查不会重复通知。

退出时立即释放鼠标、卸载键盘监听并移除托盘图标，显示约 1.8 秒的「MouseLock 已停止」提醒后结束进程。Windows 注销和关机不等待提醒。

## 游戏与屏幕

- **手动锁定**：切换窗口后仍留在选定屏幕。
- **仅游戏在前台时自动锁定**：进程名匹配时锁定，切换到其他程序时释放。支持多个 `.exe` 名称，用分号分隔。
- **目标屏幕**：选择固定屏幕，或使用启用时光标所在屏幕；自动模式下，后者跟随游戏窗口所在屏幕。
- 临时解锁期间可跨屏，松开后回到原来锁定的屏幕。
- 目标屏幕断开时释放鼠标并显示提示。
- 退出通过统一清理路径释放鼠标、卸载钩子、移除托盘图标。

与提供的 MouseLockGame 1.x 一样，程序默认以普通权限运行。如游戏使用管理员权限且锁定失败，可手动以管理员身份运行。登录自启使用当前用户的 Windows 启动项。

## 现有设置迁移

首次运行时优先读取 `%LOCALAPPDATA%\MouseLockGame\config-v2.json`；若不存在，导入同目录的旧版 `settings.ini`，保留开关状态、F12/F10 等自定义按键、手动 / 游戏模式、目标屏幕、游戏进程、托盘和自启选项。旧 INI 不会被改动。没有游戏版配置时，可读取 exe 旁的原版 `config.json`。

新版设置保存到 `config-v2.json`，采用临时文件写入后替换。缺少退出设置的旧配置使用 F9 / 2 秒。若旧版已经将 F9 用作其他功能，则保留旧绑定并将新增退出绑定留空，可在设置中重新指定。

命令行：`--show` 强制显示窗口，`--tray` 收起到托盘，`--paused` 本次暂停锁定。

## 编译与验证

Windows 10/11，Visual Studio 2022 Build Tools（C++ 与资源编译器）：

```bat
cd native
build.bat
```

生成 `native\MouseLockGame.exe`。也可以使用 MinGW-w64 在 Linux 交叉编译：

```sh
native/build-mingw.sh
```

可用 `CXX`、`WINDRES`、`PREPROCESSOR` 指定编译器，生成 `build/MouseLockGame.exe` 和 Windows 配置测试程序。发布程序静态链接 C++ 运行库，不依赖 .NET、浏览器或附加 DLL。

连续长按行为测试：

```sh
g++ -std=c++17 -Wall -Wextra -Werror native/tests/hold_exit_test.cpp -o /tmp/mouselock-hold-test
/tmp/mouselock-hold-test
```

在 Windows 上运行配置测试，将测试数据放到一个临时文件夹：

```powershell
.\build\config-test.exe "$env:TEMP\MouseLock-config-test"
```

界面冒烟测试：指定已存在的输出文件夹。程序以独立预览实例运行，不注册全局快捷键、不锁鼠标、不读写用户配置；生成主窗口与两页设置的截图，然后通过正式退出入口关闭：

```powershell
.\build\MouseLockGame.exe --ui-smoke-test "C:\Temp\MouseLock-preview"
```

通知验证：`native/tests/notification_state_test.cpp` 覆盖状态变化与重复通知过滤；Windows 上运行 `MouseLockGame.exe --notification-smoke-test <输出文件夹>`，验证真实弹窗显示、焦点保持、连续替换、不同屏幕缩放、自动消失、隐藏主窗口后的通知，以及设置页打开时先停止输入处理、显示退出提醒再结束进程。测试不锁鼠标、不读写用户配置。

使用 PowerShell 测试脚本可以自动创建截图目录、等待测试完成并检查退出码：

```powershell
.\native\tests\run-notification-smoke.ps1
```

代码中的 `hold_exit.h` 独立处理连续长按手势；`main.cpp` 负责全局按键、游戏前台检测和统一退出；`theme.cpp` 与 `settings.cpp` 负责界面；`config.cpp` 负责迁移、校验和原子保存。

## 许可

沿用上游 MIT 许可，见 [LICENSE](LICENSE)。

// Native Win32 MouseLock Game: global shortcuts, game focus, tray and warm UI.
#include "theme.h"
#include "config.h"
#include "locker.h"
#include "settings.h"
#include "hold_exit.h"
#include "notifications.h"
#include "notification_state.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <array>
#include <algorithm>
#include <cwchar>

namespace
{
    constexpr UINT WM_EXIT_REQUEST=WM_APP+10, WM_REFRESH_LOCK=WM_APP+11, WM_EMERGENCY=WM_APP+12, WM_REOPEN=WM_APP+13;
    constexpr UINT TimerExit=10, TimerLock=11, TimerFinishExit=12, TimerPreview=90, TimerNotificationPreview=91;
    constexpr UINT ExitNoticeMs=1800;
    constexpr int HotkeyToggle=1, HotkeyLeft=2, HotkeyRight=3;
    const wchar_t* MainClass=L"MouseLockGame.Main.v2";
    AppConfig cfg;
    Locker locker;
    HoldToExit exitGesture;
    Ui::Theme theme;
    NotificationPopup notification;
    LockNoticeState lastNoticeState;
    HWND mainWindow=nullptr;
    HINSTANCE instance=nullptr;
    HICON appIcon=nullptr;
    HHOOK keyboardHook=nullptr;
    std::vector<HWINEVENTHOOK> eventHooks;
    std::vector<Ui::Placement> controls;
    std::array<bool,256> held{};
    NOTIFYICONDATAW tray{};
    HMONITOR targetMonitor=nullptr;
    HWND cachedForeground=nullptr;
    DWORD cachedPid=0;
    bool cachedGame=false, shuttingDown=false, trayAdded=false, preview=false, rearmShortcuts=false;
    bool temporarilyUnlocked=false;
    bool noticesReady=false, notificationPreview=false;
    bool previewNoFocusSteal=true;
    int notificationPreviewStep=0, previewExitCode=0;
    HWND previewForeground=nullptr;
    ULONGLONG exitStarted=0;
    UINT taskbarCreated=0;
    std::wstring configPath, snapshots, runtimeMessage, lockMessage;

    bool Down(int key) { return key>0 && key<256 && (GetAsyncKeyState(key)&0x8000)!=0; }
    bool Contains(const Hotkey& hk,Modifier modifier)
    { return std::find(hk.modifiers.begin(),hk.modifiers.end(),modifier)!=hk.modifiers.end(); }
    bool HotkeyDown(const Hotkey& hk,bool physical=true)
    {
        if(!IsSet(hk) || (hk.key==0 && hk.modifiers.empty())) return false;
        auto down=[&](int vk) { return physical?Down(vk):held[vk]; };
        if(down(VK_LWIN)||down(VK_RWIN)) return false;
        if((down(VK_LCONTROL)||down(VK_RCONTROL)||down(VK_CONTROL))!=Contains(hk,Modifier::Control)) return false;
        if((down(VK_LMENU)||down(VK_RMENU)||down(VK_MENU))!=Contains(hk,Modifier::Alt)) return false;
        if((down(VK_LSHIFT)||down(VK_RSHIFT)||down(VK_SHIFT))!=Contains(hk,Modifier::Shift)) return false;
        return hk.key==0 || down(hk.key);
    }
    std::wstring KeyText(const Hotkey& hk)
    { auto value=FormatHotkey(hk); return value.empty()?L"未设置":value; }
    void SyncUi();
    void ApplyLock();
    void RequestExit(bool notify=true);

    HMONITOR CursorMonitor()
    { POINT point{}; GetCursorPos(&point); return MonitorFromPoint(point,MONITOR_DEFAULTTOPRIMARY); }
    HMONITOR ConfiguredMonitor()
    {
        if(cfg.monitor.empty()) return nullptr;
        for(const auto& display:GetDisplays()) if(display.device==cfg.monitor) return display.handle;
        return nullptr;
    }
    bool ForegroundGame(HWND& foreground)
    {
        foreground=GetForegroundWindow(); DWORD pid=0;
        GetWindowThreadProcessId(foreground,&pid);
        if(foreground==cachedForeground && pid==cachedPid) return cachedGame;
        cachedForeground=foreground; cachedPid=pid; cachedGame=false;
        if(!foreground || pid==GetCurrentProcessId() || IsIconic(foreground)) return false;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!process) return false;
        wchar_t path[32768]{}; DWORD length=32768;
        if(QueryFullProcessImageNameW(process,0,path,&length))
        {
            std::wstring name(path,length); auto slash=name.find_last_of(L"\\/");
            if(slash!=std::wstring::npos) name=name.substr(slash+1);
            for(const auto& game:ProcessNames(cfg.processes)) if(_wcsicmp(name.c_str(),game.c_str())==0) { cachedGame=true; break; }
        }
        CloseHandle(process); return cachedGame;
    }
    std::wstring StateText()
    {
        if(!runtimeMessage.empty()) return runtimeMessage;
        if(!lockMessage.empty()) return lockMessage;
        if(!cfg.enabled) return L"鼠标可以在屏幕之间自由移动";
        if(temporarilyUnlocked) return L"临时解锁中 · 松开按键回到原屏幕";
        if(cfg.gameOnly && !locker.IsLocked()) return L"等待游戏进入前台";
        return L"鼠标已留在目标屏幕，安心专注";
    }
    std::wstring MonitorName()
    {
        MONITORINFOEXW info{}; info.cbSize=sizeof(info);
        std::wstring device=cfg.monitor;
        if(targetMonitor && GetMonitorInfoW(targetMonitor,&info)) device=info.szDevice;
        auto position=device.find(L"DISPLAY");
        return position==std::wstring::npos?L"当前屏幕":L"屏幕 "+device.substr(position+7);
    }
    LockNoticeState CurrentNoticeState()
    {
        LockNoticeState state;
        state.gameOnly=cfg.gameOnly;
        state.monitor=cfg.monitor+L"|"+MonitorName();
        state.error=lockMessage;
        if(!cfg.enabled) state.status=LockNoticeStatus::Paused;
        else if(!lockMessage.empty()) state.status=LockNoticeStatus::Error;
        else if(temporarilyUnlocked) state.status=LockNoticeStatus::Temporary;
        else if(locker.IsLocked()) state.status=LockNoticeStatus::Locked;
        else state.status=LockNoticeStatus::Waiting;
        return state;
    }
    void AnnounceChange(const LockNoticeState& before,const LockNoticeState& after)
    {
        auto change=NoticeChange(before,after);
        if(change==LockNoticeChange::None) return;
        std::wstring title,detail;
        if(change==LockNoticeChange::Mode)
        {
            title=after.gameOnly?L"已切换为游戏模式":L"已切换为手动模式";
            detail=after.gameOnly?L"指定游戏在前台时自动锁定，离开游戏时释放。":L"启用锁定后，切换窗口也会留在目标屏幕。";
        }
        else if(change==LockNoticeChange::Monitor)
        { title=L"目标屏幕已切换"; detail=L"目标："+MonitorName()+L"。"; }
        else switch(after.status)
        {
        case LockNoticeStatus::Paused:
            title=L"已暂停鼠标锁定"; detail=L"鼠标已释放，可以在屏幕之间自由移动。"; break;
        case LockNoticeStatus::Locked:
            title=before.status==LockNoticeStatus::Temporary?L"已恢复鼠标锁定":L"已启用鼠标锁定";
            detail=L"目标："+MonitorName()+L"。";
            if(IsSet(cfg.unlock)) detail+=L"按住 "+KeyText(cfg.unlock)+L" 可临时解锁。";
            break;
        case LockNoticeStatus::Temporary:
            title=L"鼠标已临时解锁"; detail=L"松开 "+KeyText(cfg.unlock)+L"，回到原来锁定的屏幕。"; break;
        case LockNoticeStatus::Waiting:
            title=L"正在等待游戏"; detail=L"鼠标已释放，进入指定游戏后自动恢复锁定。"; break;
        case LockNoticeStatus::Error:
            title=L"鼠标锁定未生效"; detail=after.error; break;
        }
        notification.Show(mainWindow,title,detail,targetMonitor);
    }
    void RefreshNotifications()
    {
        auto current=CurrentNoticeState();
        auto before=lastNoticeState;
        lastNoticeState=current;
        if(noticesReady && !preview && !shuttingDown && !SettingsIsRecording()) AnnounceChange(before,current);
    }
    void AnnounceStartup(bool reopened=false)
    {
        std::wstring detail=StateText()+L"。";
        if(IsSet(cfg.exit)) detail+=L"长按 "+KeyText(cfg.exit)+L" "+std::to_wstring(cfg.exitHoldMs/1000)+L" 秒退出。";
        notification.Show(mainWindow,reopened?L"MouseLock 正在运行":L"MouseLock 已启动",detail,targetMonitor);
    }
    void ApplyLock()
    {
        if(preview || shuttingDown) return;
        bool wasLocked=locker.IsLocked(), wasTemporary=temporarilyUnlocked;
        std::wstring oldMessage=lockMessage;
        lockMessage.clear();
        temporarilyUnlocked=cfg.enabled && !SettingsIsRecording() && HotkeyDown(cfg.unlock);
        HWND game=nullptr;
        bool gameActive=!cfg.gameOnly || ForegroundGame(game);
        bool shouldLock=cfg.enabled && gameActive && !temporarilyUnlocked;
        HMONITOR wanted=ConfiguredMonitor();
        if(!cfg.monitor.empty() && !wanted)
        { shouldLock=false; if(cfg.enabled) lockMessage=L"目标屏幕未连接 · 已释放鼠标"; }
        if(!wanted && cfg.monitor.empty())
        {
            if(cfg.gameOnly && gameActive) wanted=MonitorFromWindow(game,MONITOR_DEFAULTTONEAREST);
            else wanted=targetMonitor?targetMonitor:CursorMonitor();
        }
        if(!shouldLock)
        { if(locker.IsLocked()) locker.Unlock(); }
        else if(!locker.IsLocked() || targetMonitor!=wanted)
        {
            targetMonitor=wanted;
            if(!locker.LockToMonitor(wanted))
            { locker.Unlock(); lockMessage=L"锁定未生效 · 请尝试以管理员身份运行"; }
        }
        else locker.Revalidate();
        if(wasLocked!=locker.IsLocked() || wasTemporary!=temporarilyUnlocked || lockMessage!=oldMessage) SyncUi();
        RefreshNotifications();
    }
    void UnregisterShortcuts()
    {
        for(int id:{HotkeyToggle,HotkeyLeft,HotkeyRight}) UnregisterHotKey(mainWindow,id);
    }
    bool RegisterShortcuts(const AppConfig& candidate,std::wstring& error)
    {
        if(preview) return true;
        UnregisterShortcuts();
        std::array<const Hotkey*,3> shortcuts{&candidate.toggle,&candidate.jumpLeft,&candidate.jumpRight};
        for(size_t i=0;i<shortcuts.size();++i)
        {
            const auto& hk=*shortcuts[i]; if(!IsSet(hk)) continue;
            UINT modifiers=MOD_NOREPEAT;
            if(Contains(hk,Modifier::Control)) modifiers|=MOD_CONTROL;
            if(Contains(hk,Modifier::Alt)) modifiers|=MOD_ALT;
            if(Contains(hk,Modifier::Shift)) modifiers|=MOD_SHIFT;
            if(!RegisterHotKey(mainWindow,static_cast<int>(i)+1,modifiers,hk.key))
            { error=KeyText(hk)+L" 已被其他程序占用，请更换快捷键。"; UnregisterShortcuts(); return false; }
        }
        return true;
    }
    std::wstring ExecutablePath()
    { wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768); return path; }
    bool SetAutoStart(bool enabled)
    {
        HKEY key=nullptr;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS) return false;
        LSTATUS status;
        if(enabled)
        {
            auto command=L"\""+ExecutablePath()+L"\" --tray";
            status=RegSetValueExW(key,L"MouseLockGame",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
        }
        else { status=RegDeleteValueW(key,L"MouseLockGame"); if(status==ERROR_FILE_NOT_FOUND) status=ERROR_SUCCESS; }
        RegCloseKey(key); return status==ERROR_SUCCESS;
    }
    bool ApplySettingsChange(const AppConfig& candidate,std::wstring& error)
    {
        if(preview) { cfg=candidate; SyncUi(); return true; }
        if(!RegisterShortcuts(candidate,error))
        {
            std::wstring ignored;
            if(!RegisterShortcuts(cfg,ignored)) { cfg.enabled=false; locker.Unlock(); runtimeMessage=ignored; SyncUi(); }
            return false;
        }
        bool autoChanged=candidate.autoStart!=cfg.autoStart;
        if(autoChanged && !SetAutoStart(candidate.autoStart))
        { error=L"无法更新 Windows 启动项，设置未更改。"; std::wstring ignored; RegisterShortcuts(cfg,ignored); return false; }
        if(!SaveConfig(configPath,candidate))
        {
            error=L"无法保存配置，请检查用户配置目录的写入权限。";
            if(autoChanged && !SetAutoStart(cfg.autoStart)) error+=L" 启动项恢复失败，请检查 Windows 启动应用。";
            std::wstring ignored; RegisterShortcuts(cfg,ignored); return false;
        }
        bool changedTarget=candidate.monitor!=cfg.monitor || candidate.gameOnly!=cfg.gameOnly || candidate.enabled!=cfg.enabled;
        cfg=candidate; cachedForeground=nullptr; runtimeMessage.clear();
        if(changedTarget) { locker.Unlock(); targetMonitor=ConfiguredMonitor(); if(!targetMonitor) targetMonitor=CursorMonitor(); }
        exitGesture.RequireRelease(); rearmShortcuts=true;
        ApplyLock(); SyncUi(); return true;
    }
    void SetEnabled(bool enabled)
    {
        cfg.enabled=enabled; runtimeMessage.clear(); exitGesture.RequireRelease();
        if(enabled) { targetMonitor=ConfiguredMonitor(); if(!targetMonitor) targetMonitor=CursorMonitor(); }
        else locker.Unlock();
        if(!preview && !SaveConfig(configPath,cfg)) runtimeMessage=L"本次状态已更改，但配置保存失败";
        ApplyLock(); SyncUi();
    }
    void Jump(int direction)
    {
        if(!cfg.enabled || !locker.IsLocked()) return;
        if(locker.Jump(direction))
        {
            targetMonitor=CursorMonitor();
            // An explicit jump changes the selected target, including fixed-screen mode.
            for(const auto& display:GetDisplays()) if(display.handle==targetMonitor) cfg.monitor=display.device;
            if(!SaveConfig(configPath,cfg)) runtimeMessage=L"已切换屏幕，但配置保存失败";
            SyncUi();
            RefreshNotifications();
        }
    }
    void OpenSettings()
    {
        ShowSettingsDialog(mainWindow,cfg,ApplySettingsChange,[](bool recording)
        {
            if(shuttingDown) return;
            exitGesture.RequireRelease(); rearmShortcuts=true;
            if(recording) UnregisterShortcuts();
            else
            {
                std::wstring error;
                if(!RegisterShortcuts(cfg,error)) { runtimeMessage=error; cfg.enabled=false; locker.Unlock(); SyncUi(); }
            }
        },snapshots);
    }
    void AddTray()
    {
        tray.cbSize=sizeof(tray); tray.hWnd=mainWindow; tray.uID=1;
        tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; tray.uCallbackMessage=WM_TRAYICON; tray.hIcon=appIcon;
        lstrcpynW(tray.szTip,L"MouseLock · 长按 F9 退出",128);
        trayAdded=Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE;
        if(trayAdded) { tray.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&tray); }
    }
    void ShowMain()
    { if(!shuttingDown) { ShowWindow(mainWindow,SW_RESTORE); SetForegroundWindow(mainWindow); } }
    void ShowTrayMenu()
    {
        HMENU menu=CreatePopupMenu();
        AppendMenuW(menu,MF_STRING,IDC_BTN_SETTINGS,L"偏好设置…");
        AppendMenuW(menu,MF_STRING,IDC_CHK_ENABLED,cfg.enabled?L"暂停锁定":L"启用锁定");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,ID_TRAY_JUMPPREV,L"跳转上一屏幕");
        AppendMenuW(menu,MF_STRING,ID_TRAY_JUMPNEXT,L"跳转下一屏幕");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        auto exitText=L"退出并释放鼠标\t"+KeyText(cfg.exit);
        AppendMenuW(menu,MF_STRING,IDC_BTN_EXIT,exitText.c_str());
        POINT point{}; GetCursorPos(&point); SetForegroundWindow(mainWindow);
        UINT action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,mainWindow,nullptr);
        DestroyMenu(menu);
        if(action) PostMessageW(mainWindow,WM_COMMAND,action,0);
        PostMessageW(mainWindow,WM_NULL,0,0);
    }
    void SyncUi()
    {
        if(!mainWindow) return;
        SetDlgItemTextW(mainWindow,IDC_CHK_ENABLED,cfg.enabled?L"暂停锁定":L"启用锁定");
        InvalidateRect(mainWindow,nullptr,FALSE);
        if(trayAdded)
        {
            auto text=L"MouseLock · "+std::wstring(locker.IsLocked()?L"已锁定":cfg.enabled?L"等待锁定":L"已暂停");
            if(IsSet(cfg.exit)) text+=L" · 长按 "+KeyText(cfg.exit)+L" 退出";
            lstrcpynW(tray.szTip,text.c_str(),128); tray.uFlags=NIF_TIP; Shell_NotifyIconW(NIM_MODIFY,&tray);
        }
    }
    void UpdateExit(bool physical)
    {
        if(shuttingDown || preview) return;
        if(SettingsIsRecording()) { exitGesture.RequireRelease(); return; }
        bool wasHolding=exitGesture.Holding();
        if(exitGesture.Update(HotkeyDown(cfg.exit,physical),GetTickCount64(),cfg.exitHoldMs))
            PostMessageW(mainWindow,WM_EXIT_REQUEST,0,0);
        if(IsWindowVisible(mainWindow) && (wasHolding||exitGesture.Holding()))
        { RECT r=theme.R(28,430,684,100); InvalidateRect(mainWindow,&r,FALSE); }
    }
    LRESULT CALLBACK KeyboardProc(int code,WPARAM wp,LPARAM lp)
    {
        if(code==HC_ACTION && !shuttingDown)
        {
            const auto* event=reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
            if(event->vkCode<held.size())
            {
                bool down=wp==WM_KEYDOWN || wp==WM_SYSKEYDOWN;
                bool up=wp==WM_KEYUP || wp==WM_SYSKEYUP;
                bool previous=held[event->vkCode];
                if(down||up) held[event->vkCode]=down;
                static const Hotkey emergency{{Modifier::Control,Modifier::Alt},VK_F12};
                if(down && !previous && event->vkCode==VK_F12 && HotkeyDown(emergency,false))
                { PostMessageW(mainWindow,WM_EMERGENCY,0,0); return 1; }
                UpdateExit(false);
            }
        }
        return CallNextHookEx(nullptr,code,wp,lp);
    }
    void CALLBACK WinEvent(HWINEVENTHOOK,DWORD event,HWND,LONG,LONG,DWORD,DWORD)
    {
        if(event==EVENT_SYSTEM_DESKTOPSWITCH) exitGesture.RequireRelease();
        if(!shuttingDown) PostMessageW(mainWindow,WM_REFRESH_LOCK,0,0);
    }
    bool InstallHooks()
    {
        for(int i=0;i<256;++i) held[i]=Down(i);
        held[VK_CONTROL]=held[VK_SHIFT]=held[VK_MENU]=false; // LL hooks report left/right modifier keys.
        keyboardHook=SetWindowsHookExW(WH_KEYBOARD_LL,KeyboardProc,instance,0);
        for(DWORD event:{EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_DESKTOPSWITCH,EVENT_SYSTEM_MOVESIZEEND})
        {
            auto hook=SetWinEventHook(event,event,nullptr,WinEvent,0,0,WINEVENT_OUTOFCONTEXT);
            if(hook) eventHooks.push_back(hook);
        }
        return keyboardHook!=nullptr;
    }
    void StopRuntime()
    {
        locker.Unlock();
        KillTimer(mainWindow,TimerExit); KillTimer(mainWindow,TimerLock);
        UnregisterShortcuts();
        if(keyboardHook) { UnhookWindowsHookEx(keyboardHook); keyboardHook=nullptr; }
        for(auto hook:eventHooks) UnhookWinEvent(hook);
        eventHooks.clear();
        if(trayAdded) { Shell_NotifyIconW(NIM_DELETE,&tray); trayAdded=false; }
    }
    void RequestExit(bool notify)
    {
        if(!IsWindow(mainWindow)) return;
        if(shuttingDown && notify) return;
        shuttingDown=true;
        // Stop all input/locking work immediately; only the short toast remains.
        StopRuntime();
        CloseSettingsForExit();
        ShowWindow(mainWindow,SW_HIDE);
        if(notify && (!preview || notificationPreview))
        {
            exitStarted=GetTickCount64();
            if(notification.Show(mainWindow,L"MouseLock 已停止",L"鼠标已释放，快捷键监听已关闭。",targetMonitor,ExitNoticeMs) &&
                SetTimer(mainWindow,TimerFinishExit,ExitNoticeMs,nullptr)) return;
        }
        DestroyWindow(mainWindow);
    }
    void PreviewCheck(bool condition)
    { if(!condition) previewExitCode=3; }
    void CheckNotificationLayout(HMONITOR monitor)
    {
        HWND popup=notification.Window();
        RECT bounds{}; GetClientRect(popup,&bounds);
        UINT dpi=GetDpiForWindow(popup);
        PreviewCheck(bounds.right==MulDiv(396,dpi,96) && bounds.bottom==MulDiv(124,dpi,96));
        PreviewCheck(MonitorFromWindow(popup,MONITOR_DEFAULTTONEAREST)==monitor);
    }
    void NotificationPreviewStep()
    {
        KillTimer(mainWindow,TimerNotificationPreview);
        HWND popup=notification.Window();
        if(notificationPreviewStep++==0)
        {
            PreviewCheck(IsWindowVisible(popup) && previewNoFocusSteal);
            PreviewCheck((GetWindowLongPtrW(popup,GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0);
            PreviewCheck(SendMessageW(popup,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE);
            PreviewCheck(Ui::SaveSnapshot(popup,snapshots+L"\\notification-startup.png"));
            auto paused=lastNoticeState, locked=paused;
            locked.status=LockNoticeStatus::Locked;
            HWND foreground=GetForegroundWindow();
            unsigned revision=notification.Revision();
            AnnounceChange(paused,locked);
            PreviewCheck(notification.Window()==popup && notification.Revision()==revision+1);
            PreviewCheck(GetForegroundWindow()==foreground);
            PreviewCheck(Ui::SaveSnapshot(popup,snapshots+L"\\notification-locked.png"));
            AnnounceChange(locked,locked);
            PreviewCheck(notification.Revision()==revision+1);
            auto temporary=locked; temporary.status=LockNoticeStatus::Temporary;
            AnnounceChange(locked,temporary);
            PreviewCheck(Ui::SaveSnapshot(popup,snapshots+L"\\notification-temporary.png"));
            AnnounceChange(temporary,locked);
            PreviewCheck(Ui::SaveSnapshot(popup,snapshots+L"\\notification-restored.png"));
            AnnounceChange(locked,paused);
            PreviewCheck(notification.Window()==popup && notification.Revision()==revision+4);
            PreviewCheck(Ui::SaveSnapshot(popup,snapshots+L"\\notification-paused.png"));
            int screen=0;
            for(const auto& display:GetDisplays())
            {
                foreground=GetForegroundWindow();
                for(int repeat=0;repeat<2;++repeat)
                {
                    notification.Show(mainWindow,L"目标屏幕已切换",L"通知会按目标屏幕的缩放比例显示。",display.handle);
                    CheckNotificationLayout(display.handle);
                    PreviewCheck(GetForegroundWindow()==foreground);
                }
                PreviewCheck(Ui::SaveSnapshot(popup,snapshots+L"\\notification-screen-"+std::to_wstring(++screen)+L".png"));
            }
            SetTimer(mainWindow,TimerNotificationPreview,2800,nullptr);
        }
        else
        {
            PreviewCheck(!IsWindowVisible(popup)); // dismisses without interaction
            ShowWindow(mainWindow,SW_HIDE);
            HWND foreground=GetForegroundWindow();
            auto locked=lastNoticeState; locked.status=LockNoticeStatus::Locked;
            AnnounceChange(lastNoticeState,locked);
            PreviewCheck(IsWindowVisible(popup) && GetForegroundWindow()==foreground);
            // Existing settings smoke mode requests a normal exit from its modal loop.
            OpenSettings();
            PreviewCheck(shuttingDown && !SettingsIsOpen() && !IsWindowVisible(mainWindow));
            PreviewCheck(!locker.IsLocked() && !keyboardHook && eventHooks.empty() && !trayAdded);
            PreviewCheck(IsWindowVisible(notification.Window()));
            PreviewCheck(Ui::SaveSnapshot(notification.Window(),snapshots+L"\\notification-exit.png"));
            auto started=exitStarted;
            RequestExit(); // repeated requests must not restart the exit countdown
            PreviewCheck(exitStarted==started);
        }
    }
    void PaintMain()
    {
        Ui::Paint p(mainWindow,theme); HDC dc=p.dc; const auto& t=theme;
        Ui::Label(dc,t,L"MOUSELOCK  /  GAME EDITION",28,21,520,22,t.small,Ui::Muted);
        Ui::Label(dc,t,L"MouseLock",28,49,520,49,t.display);
        Ui::Label(dc,t,L"留在这一屏，专注眼前的事。",30,102,600,28,t.body,Ui::Muted);
        Ui::Round(dc,t.R(602,57,110,30),Ui::AccentSoft,Ui::AccentSoft,t.S(15));
        Ui::Label(dc,t,L"轻盈 · 本地",602,57,110,30,t.small,Ui::Accent,DT_CENTER);
        Ui::Card(dc,t,28,152,684,162);
        std::wstring title=!cfg.enabled?L"准备好，进入专注。":temporarilyUnlocked?L"暂时，自由一下。":L"专注，不越界。";
        Ui::Label(dc,t,title,52,174,450,34,t.heading);
        Ui::Label(dc,t,StateText(),52,217,462,26,t.body,Ui::Muted);
        Ui::Line(dc,t,52,264,636);
        auto mode=cfg.gameOnly?L"游戏前台自动锁定":L"手动锁定";
        Ui::Label(dc,t,mode,52,274,202,25,t.small,Ui::Muted);
        std::wstring monitorText=L"目标 · 跟随当前屏幕";
        if(!cfg.monitor.empty())
        {
            auto position=cfg.monitor.find(L"DISPLAY");
            monitorText=L"目标 · 屏幕 "+(position==std::wstring::npos?cfg.monitor:cfg.monitor.substr(position+7));
        }
        Ui::Label(dc,t,monitorText,338,274,350,25,t.small,Ui::Muted,DT_RIGHT);
        Ui::Card(dc,t,28,334,684,216);
        Ui::Label(dc,t,L"随手掌控",52,352,400,28);
        Ui::Label(dc,t,L"全局快捷键",504,352,184,28,t.small,Ui::Muted,DT_RIGHT);
        Ui::Line(dc,t,52,393,636);
        Ui::Label(dc,t,L"切换锁定",52,409,174,32);
        Ui::Keycap(dc,t,KeyText(cfg.toggle),234,409,102);
        Ui::Label(dc,t,L"按住临时解锁",372,409,190,32);
        Ui::Keycap(dc,t,KeyText(cfg.unlock),578,409,110);
        Ui::Line(dc,t,52,457,636);
        Ui::Keycap(dc,t,KeyText(cfg.exit),52,477,90);
        auto exitText=IsSet(cfg.exit)?L"按住 "+std::to_wstring(cfg.exitHoldMs/1000)+L" 秒，退出程序":L"长按退出已停用";
        if(exitGesture.Holding())
        {
            unsigned elapsed=exitGesture.Elapsed(GetTickCount64(),cfg.exitHoldMs);
            wchar_t countdown[100]{};
            swprintf(countdown,100,L"继续按住 · %.1f 秒后退出",(cfg.exitHoldMs-elapsed)/1000.0);
            exitText=countdown;
            Ui::Round(dc,t.R(162,520,526,4),Ui::Border,Ui::Border,t.S(2));
            int width=static_cast<int>(526ULL*elapsed/cfg.exitHoldMs);
            if(width>2) Ui::Round(dc,t.R(162,520,width,4),Ui::Accent,Ui::Accent,t.S(2));
        }
        Ui::Label(dc,t,exitText,162,473,526,27,nullptr,Ui::Accent);
        Ui::Label(dc,t,L"松开即可取消，退出时自动释放鼠标。",162,501,526,22,t.small,Ui::Muted);
        Ui::Label(dc,t,L"关闭窗口后继续在托盘运行。",28,623,510,24,t.small,Ui::Muted);
        Ui::Label(dc,t,L"紧急解除  Ctrl + Alt + F12",28,649,510,22,t.small,Ui::Muted);
    }
    void CreateControls()
    {
        auto add=[&](int id,const wchar_t* text,int x,int y,int w,int h,bool primary=false)
        { auto button=Ui::Button(mainWindow,id,text,primary); controls.push_back({button,x,y,w,h}); };
        add(IDC_CHK_ENABLED,L"启用锁定",538,198,150,42,true);
        SetPropW(GetDlgItem(mainWindow,IDC_CHK_ENABLED),L"UiOnPanel",reinterpret_cast<HANDLE>(1));
        add(IDC_BTN_SETTINGS,L"偏好设置",28,574,334,42);
        add(IDC_BTN_HIDE,L"收起到托盘",378,574,334,42);
        add(IDC_BTN_EXIT,L"退出程序",594,634,118,34);
        Ui::Layout(theme,controls);
    }
    LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp)
    {
        if(taskbarCreated && message==taskbarCreated && !shuttingDown) { trayAdded=false; AddTray(); SyncUi(); return 0; }
        switch(message)
        {
        case WM_CREATE:
            mainWindow=hwnd; theme.SetDpi(GetDpiForWindow(hwnd)); Ui::WindowChrome(hwnd); CreateControls(); return 0;
        case WM_PAINT: PaintMain(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DRAWITEM: Ui::DrawButton(theme,*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
        case WM_DPICHANGED:
        {
            theme.SetDpi(HIWORD(wp)); Ui::Layout(theme,controls); RECT r=*reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
            InvalidateRect(hwnd,nullptr,FALSE); return 0;
        }
        case WM_COMMAND:
            if(shuttingDown) return 0;
            switch(LOWORD(wp))
            {
            case IDC_CHK_ENABLED: SetEnabled(!cfg.enabled); break;
            case IDC_BTN_SETTINGS: ShowMain(); OpenSettings(); break;
            case IDC_BTN_HIDE: ShowWindow(hwnd,SW_HIDE); break;
            case IDC_BTN_EXIT: RequestExit(); break;
            case ID_TRAY_JUMPPREV: Jump(-1); break;
            case ID_TRAY_JUMPNEXT: Jump(1); break;
            }
            return 0;
        case WM_HOTKEY:
            if(!SettingsIsRecording() && !rearmShortcuts && !shuttingDown)
            {
                if(wp==HotkeyToggle) SetEnabled(!cfg.enabled);
                if(wp==HotkeyLeft) Jump(-1);
                if(wp==HotkeyRight) Jump(1);
            }
            return 0;
        case WM_EMERGENCY:
            if(!shuttingDown)
            {
                SetEnabled(false); exitGesture.RequireRelease();
                notification.Show(hwnd,L"已紧急解除锁定",L"鼠标已释放；按切换快捷键可以重新启用。",targetMonitor);
            }
            return 0;
        case WM_EXIT_REQUEST: RequestExit(); return 0;
        case WM_REOPEN: if(!shuttingDown) { ShowMain(); AnnounceStartup(true); } return 0;
        case WM_REFRESH_LOCK: ApplyLock(); return 0;
        case WM_DISPLAYCHANGE: targetMonitor=nullptr; cachedForeground=nullptr; locker.Unlock(); ApplyLock(); SyncUi(); return 0;
        case WM_POWERBROADCAST:
            exitGesture.RequireRelease();
            if(wp==PBT_APMSUSPEND) locker.Unlock();
            return TRUE;
        case WM_TIMER:
            if(wp==TimerFinishExit)
            {
                if(notificationPreview) PreviewCheck(GetTickCount64()-exitStarted>=ExitNoticeMs);
                KillTimer(hwnd,TimerFinishExit); DestroyWindow(hwnd); return 0;
            }
            if(shuttingDown) return 0;
            if(wp==TimerNotificationPreview) { NotificationPreviewStep(); return 0; }
            if(wp==TimerExit)
            {
                if(rearmShortcuts && !HotkeyDown(cfg.toggle) && !HotkeyDown(cfg.jumpLeft) && !HotkeyDown(cfg.jumpRight)) rearmShortcuts=false;
                UpdateExit(true);
            }
            if(wp==TimerLock)
            { ApplyLock(); if(!trayAdded) { AddTray(); SyncUi(); } }
            if(wp==TimerPreview)
            {
                KillTimer(hwnd,TimerPreview);
                RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
                bool ok=Ui::SaveSnapshot(hwnd,snapshots+L"\\main.png");
                OpenSettings();
                // Exercise the same orderly shutdown entry point as the hold gesture.
                PostMessageW(hwnd,WM_EXIT_REQUEST,0,0);
                if(!ok) runtimeMessage=L"snapshot failed";
            }
            return 0;
        case WM_TRAYICON:
            if(shuttingDown) return 0;
            if(LOWORD(lp)==WM_CONTEXTMENU || LOWORD(lp)==WM_RBUTTONUP) ShowTrayMenu();
            else if(LOWORD(lp)==WM_LBUTTONDBLCLK || LOWORD(lp)==NIN_SELECT || LOWORD(lp)==NIN_KEYSELECT) ShowMain();
            return 0;
        case WM_CLOSE: ShowWindow(hwnd,SW_HIDE); return 0;
        case WM_QUERYENDSESSION: shuttingDown=true; locker.Unlock(); return TRUE;
        case WM_ENDSESSION: if(wp) RequestExit(false); else shuttingDown=false; return 0;
        case WM_DESTROY:
            shuttingDown=true; StopRuntime(); notification.Close();
            KillTimer(hwnd,TimerFinishExit); KillTimer(hwnd,TimerPreview); KillTimer(hwnd,TimerNotificationPreview);
            PostQuitMessage(runtimeMessage==L"snapshot failed"?2:previewExitCode); return 0;
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    void LoadSettings()
    {
        PWSTR local=nullptr;
        if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))
        {
            std::wstring directory=std::wstring(local)+L"\\MouseLockGame"; CoTaskMemFree(local);
            CreateDirectoryW(directory.c_str(),nullptr); configPath=directory+L"\\config-v2.json";
            if(GetFileAttributesW(configPath.c_str())!=INVALID_FILE_ATTRIBUTES) cfg=LoadConfig(configPath);
            else if(GetFileAttributesW((directory+L"\\settings.ini").c_str())!=INVALID_FILE_ATTRIBUTES) cfg=ImportLegacyConfig(directory+L"\\settings.ini");
            else
            {
                auto exe=ExecutablePath(); auto old=exe.substr(0,exe.find_last_of(L"\\/")+1)+L"config.json";
                if(GetFileAttributesW(old.c_str())!=INVALID_FILE_ATTRIBUTES) cfg=LoadConfig(old);
            }
        }
    }
}

int WINAPI wWinMain(HINSTANCE appInstance,HINSTANCE,PWSTR,int)
{
    instance=appInstance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    bool forceTray=false,forceShow=false,paused=false;
    for(int i=1;i<argc;++i)
    {
        if(wcscmp(argv[i],L"--tray")==0) forceTray=true;
        if(wcscmp(argv[i],L"--show")==0) forceShow=true;
        if(wcscmp(argv[i],L"--paused")==0) paused=true;
        if(wcscmp(argv[i],L"--ui-smoke-test")==0 && i+1<argc) { preview=true; snapshots=argv[++i]; }
        if(wcscmp(argv[i],L"--notification-smoke-test")==0 && i+1<argc)
        { preview=true; notificationPreview=true; snapshots=argv[++i]; }
    }
    LocalFree(argv);
    HANDLE mutex=CreateMutexW(nullptr,TRUE,preview?L"Local\\MouseLockGame.Preview.v2":L"Local\\MouseLockGame.Instance.v1");
    if(!mutex) return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS)
    {
        HWND existing=FindWindowW(MainClass,nullptr);
        if(!existing) existing=FindWindowW(L"MouseLockGame.Main.v1",nullptr);
        if(existing) { ShowWindow(existing,SW_RESTORE); SetForegroundWindow(existing); PostMessageW(existing,WM_REOPEN,0,0); }
        CloseHandle(mutex); return 0;
    }
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    Gdiplus::GdiplusStartupInput gdiplusInput; ULONG_PTR gdiplusToken=0;
    if(Gdiplus::GdiplusStartup(&gdiplusToken,&gdiplusInput,nullptr)!=Gdiplus::Ok) { CloseHandle(mutex); return 1; }
    if(!preview) LoadSettings();
    if(paused) cfg.enabled=false;
    exitGesture.RequireRelease();
    appIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_MOUSELOCK),IMAGE_ICON,32,32,LR_DEFAULTCOLOR));
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=WindowProc; wc.hInstance=instance;
    wc.hIcon=appIcon; wc.hIconSm=appIcon; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName=MainClass;
    RegisterClassExW(&wc);
    theme.SetDpi(GetDpiForSystem()); RECT r=theme.R(0,0,740,696);
    DWORD style=WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&r,style,FALSE,WS_EX_CONTROLPARENT,theme.dpi);
    mainWindow=CreateWindowExW(WS_EX_CONTROLPARENT,MainClass,L"MouseLock",style,
        CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
    if(!mainWindow) { Gdiplus::GdiplusShutdown(gdiplusToken); CloseHandle(mutex); return 1; }
    if(!preview)
    {
        AddTray();
        std::wstring error;
        if(!InstallHooks()) runtimeMessage=L"键盘监听未能启动 · 请重新打开程序";
        if(!RegisterShortcuts(cfg,error)) { cfg.enabled=false; runtimeMessage=error; }
        if(!SetTimer(mainWindow,TimerExit,25,nullptr) || !SetTimer(mainWindow,TimerLock,125,nullptr))
        { RequestExit(false); Gdiplus::GdiplusShutdown(gdiplusToken); CloseHandle(mutex); return 1; }
        targetMonitor=ConfiguredMonitor(); if(!targetMonitor) targetMonitor=CursorMonitor();
        ApplyLock();
    }
    SyncUi();
    if(preview || forceShow || (!cfg.startSilent && !forceTray)) ShowMain();
    lastNoticeState=CurrentNoticeState(); noticesReady=true;
    if(!preview || notificationPreview)
    {
        previewForeground=GetForegroundWindow(); AnnounceStartup();
        previewNoFocusSteal=GetForegroundWindow()==previewForeground;
    }
    if(preview && !notificationPreview) SetTimer(mainWindow,TimerPreview,400,nullptr);
    if(notificationPreview) SetTimer(mainWindow,TimerNotificationPreview,200,nullptr);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0)
    {
        if(!IsDialogMessageW(mainWindow,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if(appIcon) DestroyIcon(appIcon);
    Gdiplus::GdiplusShutdown(gdiplusToken); ReleaseMutex(mutex); CloseHandle(mutex);
    return static_cast<int>(message.wParam);
}

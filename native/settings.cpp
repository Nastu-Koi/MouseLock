#include "settings.h"
#include "theme.h"
#include "locker.h"
#include "resource.h"
#include <commctrl.h>
#include <algorithm>
#include <array>

namespace
{
    constexpr int CaptureBase=2000, RestoreBase=2010, ClearBase=2020;
    const wchar_t* Labels[]={L"切换锁定",L"按住临时解锁",L"长按退出程序",L"跳转上一屏幕",L"跳转下一屏幕"};
    const wchar_t* Descriptions[]={L"按一下启用，再按一下暂停",L"松开后回到原来锁定的屏幕",L"松开即取消；退出前自动释放鼠标",L"把锁定一起移到相邻屏幕",L"把锁定一起移到相邻屏幕"};
    struct Context
    {
        AppConfig cfg;
        Ui::Theme theme;
        HWND hwnd=nullptr, owner=nullptr;
        ApplySettings apply;
        std::function<void(bool)> onCapture;
        std::vector<Ui::Placement> layout;
        std::vector<HWND> shortcutControls, generalControls;
        std::vector<DisplayInfo> displays;
        int tab=0, recording=-1;
        std::vector<Modifier> capturedModifiers;
        std::wstring status=L"修改后自动保存", snapshots;
        bool error=false, ownerExiting=false;
    };
    Context* ctx=nullptr;
    Hotkey& Binding(AppConfig& cfg,int index)
    {
        switch(index) { case 0:return cfg.toggle; case 1:return cfg.unlock; case 2:return cfg.exit;
                        case 3:return cfg.jumpLeft; default:return cfg.jumpRight; }
    }
    bool ModifierKey(UINT key)
    {
        return key==VK_CONTROL || key==VK_SHIFT || key==VK_MENU ||
               (key>=VK_LSHIFT && key<=VK_RMENU);
    }
    std::vector<Modifier> Modifiers()
    {
        std::vector<Modifier> result;
        if(GetKeyState(VK_CONTROL)&0x8000) result.push_back(Modifier::Control);
        if(GetKeyState(VK_MENU)&0x8000) result.push_back(Modifier::Alt);
        if(GetKeyState(VK_SHIFT)&0x8000) result.push_back(Modifier::Shift);
        return result;
    }
    void UpdateControls()
    {
        for(int i=0;i<5;++i)
        {
            auto text=FormatHotkey(Binding(ctx->cfg,i));
            if(text.empty()) text=L"未设置";
            if(ctx->recording==i) text=L"请按下快捷键…";
            SetDlgItemTextW(ctx->hwnd,CaptureBase+i,text.c_str());
        }
        SendDlgItemMessageW(ctx->hwnd,IDC_EXIT_DURATION,CB_SETCURSEL,ctx->cfg.exitHoldMs/1000-1,0);
        SendDlgItemMessageW(ctx->hwnd,IDC_MODE,CB_SETCURSEL,ctx->cfg.gameOnly?1:0,0);
        int selected=0;
        for(size_t i=0;i<ctx->displays.size();++i)
            if(ctx->cfg.monitor==ctx->displays[i].device) selected=static_cast<int>(i)+1;
        SendDlgItemMessageW(ctx->hwnd,IDC_MONITOR,CB_SETCURSEL,selected,0);
        SetDlgItemTextW(ctx->hwnd,IDC_PROCESSES,ctx->cfg.processes.c_str());
        SendDlgItemMessageW(ctx->hwnd,IDC_CHK_SILENT,BM_SETCHECK,ctx->cfg.startSilent?BST_CHECKED:BST_UNCHECKED,0);
        SendDlgItemMessageW(ctx->hwnd,IDC_CHK_AUTOSTART,BM_SETCHECK,ctx->cfg.autoStart?BST_CHECKED:BST_UNCHECKED,0);
        InvalidateRect(ctx->hwnd,nullptr,FALSE);
    }
    void EndRecording()
    {
        if(!ctx || ctx->recording<0) return;
        ctx->recording=-1;
        ctx->onCapture(false);
        UpdateControls();
    }
    bool Commit(const AppConfig& candidate)
    {
        std::wstring error;
        bool ok=ValidateConfig(candidate,error) && ctx->apply(candidate,error);
        if(ok) ctx->cfg=candidate;
        ctx->error=!ok;
        ctx->status=ok?L"设置已保存":error;
        UpdateControls();
        return ok;
    }
    void FinishKey(Hotkey hotkey)
    {
        int index=ctx->recording;
        AppConfig candidate=ctx->cfg;
        Binding(candidate,index)=std::move(hotkey);
        EndRecording();
        Commit(candidate);
        SetFocus(GetDlgItem(ctx->hwnd,IDC_BTN_OK));
    }
    LRESULT CALLBACK CaptureProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)
    {
        if(ctx && ctx->recording==GetDlgCtrlID(hwnd)-CaptureBase)
        {
            if(msg==WM_GETDLGCODE) return DLGC_WANTALLKEYS;
            if(msg==WM_KILLFOCUS) EndRecording();
            if(msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN)
            {
                if(wp==VK_ESCAPE) { EndRecording(); return 0; }
                if(wp==VK_BACK) { FinishKey(Hotkey{{},-1}); return 0; }
                if(wp==VK_LWIN || wp==VK_RWIN || (GetKeyState(VK_LWIN)&0x8000) || (GetKeyState(VK_RWIN)&0x8000))
                { ctx->status=L"请选择不含 Windows 键的快捷键。"; ctx->error=true; InvalidateRect(ctx->hwnd,nullptr,FALSE); return 0; }
                auto mods=Modifiers();
                if(ModifierKey(static_cast<UINT>(wp))) { ctx->capturedModifiers=mods; return 0; }
                FinishKey(Hotkey{mods,static_cast<int>(wp)});
                return 0;
            }
            if(msg==WM_KEYUP || msg==WM_SYSKEYUP)
            {
                if(ModifierKey(static_cast<UINT>(wp)) && !ctx->capturedModifiers.empty())
                    FinishKey(Hotkey{ctx->capturedModifiers,0});
                return 0;
            }
        }
        if(msg==WM_NCDESTROY) RemoveWindowSubclass(hwnd,CaptureProc,2);
        return DefSubclassProc(hwnd,msg,wp,lp);
    }
    HWND AddButton(int id,const wchar_t* text,int x,int y,int w,int h,bool primary=false,int tab=-1)
    {
        HWND button=Ui::Button(ctx->hwnd,id,text,primary);
        if(tab>=0) SetPropW(button,L"UiOnPanel",reinterpret_cast<HANDLE>(1));
        ctx->layout.push_back({button,x,y,w,h});
        if(tab==0) ctx->shortcutControls.push_back(button);
        if(tab==1) ctx->generalControls.push_back(button);
        return button;
    }
    HWND AddControl(const wchar_t* cls,int id,const wchar_t* text,DWORD style,int x,int y,int w,int h,int tab)
    {
        HWND control=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,0,0,
            ctx->hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        ctx->layout.push_back({control,x,y,w,h});
        (tab==0?ctx->shortcutControls:ctx->generalControls).push_back(control);
        return control;
    }
    void SelectTab(int tab)
    {
        EndRecording(); ctx->tab=tab;
        for(HWND h:ctx->shortcutControls) ShowWindow(h,tab==0?SW_SHOW:SW_HIDE);
        for(HWND h:ctx->generalControls) ShowWindow(h,tab==1?SW_SHOW:SW_HIDE);
        for(int i=0;i<2;++i)
        {
            HWND h=GetDlgItem(ctx->hwnd,i==0?IDC_TAB_SHORTCUTS:IDC_TAB_GENERAL);
            if(i==tab) SetPropW(h,L"UiPrimary",reinterpret_cast<HANDLE>(1)); else RemovePropW(h,L"UiPrimary");
            InvalidateRect(h,nullptr,FALSE);
        }
        InvalidateRect(ctx->hwnd,nullptr,FALSE);
    }
    void CreateControls()
    {
        AddButton(IDC_TAB_SHORTCUTS,L"快捷键",28,94,138,36,true);
        AddButton(IDC_TAB_GENERAL,L"锁定与启动",176,94,158,36);
        for(int i=0;i<5;++i)
        {
            int y=162+64*i;
            HWND capture=AddButton(CaptureBase+i,L"",308,y,190,38,false,0);
            SetWindowSubclass(capture,CaptureProc,2,0);
            AddButton(RestoreBase+i,L"重置",508,y,64,38,false,0);
            AddButton(ClearBase+i,L"清空",580,y,64,38,false,0);
        }
        HWND duration=AddControl(L"COMBOBOX",IDC_EXIT_DURATION,L"",CBS_DROPDOWNLIST|WS_VSCROLL,308,498,190,260,0);
        for(int i=1;i<=10;++i)
        {
            auto label=std::to_wstring(i)+L" 秒";
            SendMessageW(duration,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        }
        HWND mode=AddControl(L"COMBOBOX",IDC_MODE,L"",CBS_DROPDOWNLIST,238,168,406,140,1);
        SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"手动锁定 · 切换窗口仍保持"));
        SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"仅游戏在前台时自动锁定"));
        HWND monitors=AddControl(L"COMBOBOX",IDC_MONITOR,L"",CBS_DROPDOWNLIST|WS_VSCROLL,238,228,406,220,1);
        SendMessageW(monitors,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"光标所在屏幕 / 自动跟随游戏"));
        ctx->displays=GetDisplays();
        for(const auto& display:ctx->displays) SendMessageW(monitors,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(display.label.c_str()));
        // Keep a disconnected target visible, so opening settings never silently changes it.
        if(!ctx->cfg.monitor.empty() && std::none_of(ctx->displays.begin(),ctx->displays.end(),[](const auto& d){return d.device==ctx->cfg.monitor;}))
        {
            auto label=ctx->cfg.monitor+L"（未连接）";
            ctx->displays.push_back({nullptr,{},ctx->cfg.monitor,label});
            SendMessageW(monitors,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        }
        AddControl(L"EDIT",IDC_PROCESSES,L"",ES_AUTOHSCROLL|WS_BORDER,52,321,480,34,1);
        SendDlgItemMessageW(ctx->hwnd,IDC_PROCESSES,EM_SETLIMITTEXT,2000,0);
        AddButton(IDC_SAVE_PROCESSES,L"保存列表",544,318,100,38,false,1);
        AddControl(L"BUTTON",IDC_CHK_SILENT,L"启动时收起到系统托盘",BS_AUTOCHECKBOX,52,416,590,28,1);
        AddControl(L"BUTTON",IDC_CHK_AUTOSTART,L"登录 Windows 后自动启动",BS_AUTOCHECKBOX,52,460,590,28,1);
        AddButton(IDC_RESET_ALL,L"恢复默认",28,610,120,38);
        AddButton(IDC_BTN_OK,L"完成",550,610,122,38,true);
        Ui::Layout(ctx->theme,ctx->layout);
        UpdateControls(); SelectTab(0);
    }
    void PaintWindow()
    {
        Ui::Paint p(ctx->hwnd,ctx->theme); auto& t=ctx->theme; HDC dc=p.dc;
        Ui::Label(dc,t,L"偏好设置",28,24,500,34,t.heading);
        Ui::Label(dc,t,L"让每一次操作，都顺手一点。",28,62,600,24,t.body,Ui::Muted);
        Ui::Card(dc,t,28,146,644,402);
        if(ctx->tab==0)
        {
            for(int i=0;i<5;++i)
            {
                int y=159+64*i;
                Ui::Label(dc,t,Labels[i],52,y,240,24,nullptr,i==2?Ui::Accent:Ui::Text);
                Ui::Label(dc,t,Descriptions[i],52,y+25,242,22,t.small,Ui::Muted);
                if(i<4) Ui::Line(dc,t,52,y+57,592);
            }
            Ui::Line(dc,t,52,482,592);
            Ui::Label(dc,t,L"退出前需要按住",52,492,242,34);
            Ui::Label(dc,t,L"1–10 秒",516,492,120,34,t.small,Ui::Muted);
            Ui::Label(dc,t,L"点击按键框录入 · Esc 取消 · Backspace 清空",28,556,644,25,t.small,Ui::Muted);
        }
        else
        {
            Ui::Label(dc,t,L"工作模式",52,162,172,34);
            Ui::Label(dc,t,L"目标屏幕",52,222,172,34);
            Ui::Line(dc,t,52,273,592);
            Ui::Label(dc,t,L"自动锁定的游戏",52,281,500,30);
            Ui::Label(dc,t,L"填写 .exe 进程名，多个游戏用分号分隔。",52,364,592,24,t.small,Ui::Muted);
            Ui::Line(dc,t,52,401,592);
            Ui::Label(dc,t,L"临时解锁后，松开按键会回到原来锁定的屏幕。",52,504,592,24,t.small,Ui::Muted);
            Ui::Label(dc,t,L"快捷键始终全局生效，收起窗口后也能长按退出。",28,556,644,25,t.small,Ui::Muted);
        }
        Ui::Label(dc,t,ctx->status,160,608,375,40,t.small,ctx->error?Ui::Accent:Ui::Muted);
    }
    LRESULT CALLBACK WindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
    {
        switch(msg)
        {
        case WM_CREATE:
            ctx->hwnd=hwnd; ctx->theme.SetDpi(GetDpiForWindow(hwnd)); Ui::WindowChrome(hwnd); CreateControls(); return 0;
        case WM_PAINT: PaintWindow(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DRAWITEM: Ui::DrawButton(ctx->theme,*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor(reinterpret_cast<HDC>(wp),Ui::Text);
            SetBkColor(reinterpret_cast<HDC>(wp),Ui::Panel);
            return reinterpret_cast<LRESULT>(ctx->theme.panel);
        case WM_DPICHANGED:
        {
            ctx->theme.SetDpi(HIWORD(wp)); Ui::Layout(ctx->theme,ctx->layout);
            RECT r=*reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
            InvalidateRect(hwnd,nullptr,FALSE); return 0;
        }
        case WM_COMMAND:
        {
            int id=LOWORD(wp), notification=HIWORD(wp);
            if(id>=CaptureBase && id<CaptureBase+5)
            {
                EndRecording(); ctx->recording=id-CaptureBase; ctx->capturedModifiers.clear(); ctx->onCapture(true);
                SetFocus(GetDlgItem(hwnd,id)); UpdateControls(); return 0;
            }
            AppConfig candidate=ctx->cfg;
            if(id>=RestoreBase && id<RestoreBase+5)
            {
                AppConfig defaults; Binding(candidate,id-RestoreBase)=Binding(defaults,id-RestoreBase); Commit(candidate); return 0;
            }
            if(id>=ClearBase && id<ClearBase+5)
            { Binding(candidate,id-ClearBase)=Hotkey{{},-1}; Commit(candidate); return 0; }
            switch(id)
            {
            case IDC_TAB_SHORTCUTS: SelectTab(0); break;
            case IDC_TAB_GENERAL: SelectTab(1); break;
            case IDC_EXIT_DURATION:
                if(notification==CBN_SELCHANGE) { candidate.exitHoldMs=static_cast<unsigned>(SendDlgItemMessageW(hwnd,id,CB_GETCURSEL,0,0)+1)*1000; Commit(candidate); } break;
            case IDC_MODE:
                if(notification==CBN_SELCHANGE) { candidate.gameOnly=SendDlgItemMessageW(hwnd,id,CB_GETCURSEL,0,0)==1; Commit(candidate); } break;
            case IDC_MONITOR:
                if(notification==CBN_SELCHANGE)
                {
                    int index=static_cast<int>(SendDlgItemMessageW(hwnd,id,CB_GETCURSEL,0,0));
                    candidate.monitor=index>0?ctx->displays.at(index-1).device:L""; Commit(candidate);
                } break;
            case IDC_SAVE_PROCESSES:
            {
                wchar_t text[2001]{}; GetDlgItemTextW(hwnd,IDC_PROCESSES,text,2001); candidate.processes=text; Commit(candidate); break;
            }
            case IDC_CHK_SILENT:
                candidate.startSilent=SendDlgItemMessageW(hwnd,id,BM_GETCHECK,0,0)==BST_CHECKED; Commit(candidate); break;
            case IDC_CHK_AUTOSTART:
                candidate.autoStart=SendDlgItemMessageW(hwnd,id,BM_GETCHECK,0,0)==BST_CHECKED; Commit(candidate); break;
            case IDC_RESET_ALL: EndRecording(); Commit(AppConfig{}); break;
            case IDC_BTN_OK:
            case IDCANCEL: SendMessageW(hwnd,WM_CLOSE,0,0); break;
            }
            return 0;
        }
        case WM_CLOSE: EndRecording(); DestroyWindow(hwnd); return 0;
        case WM_TIMER:
            if(wp==90)
            {
                KillTimer(hwnd,90); UpdateWindow(hwnd);
                Ui::SaveSnapshot(hwnd,ctx->snapshots+L"\\settings-shortcuts.png");
                SelectTab(1); RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
                Ui::SaveSnapshot(hwnd,ctx->snapshots+L"\\settings-general.png");
                // Exit with the modal settings window still open. This exercises
                // owner/child destruction and the nested message loop's quit path.
                PostMessageW(ctx->owner,WM_COMMAND,IDC_BTN_EXIT,0);
            }
            return 0;
        }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
}

bool SettingsIsRecording() { return ctx && ctx->recording>=0; }
bool SettingsIsOpen() { return ctx!=nullptr; }
void CloseSettingsForExit()
{
    if(!ctx) return;
    ctx->ownerExiting=true;
    EndRecording();
    if(IsWindow(ctx->hwnd)) DestroyWindow(ctx->hwnd);
}

void ShowSettingsDialog(HWND parent,const AppConfig& current,const ApplySettings& onChanged,
                        const std::function<void(bool)>& onCapture,const std::wstring& snapshotDirectory)
{
    if(ctx) { SetForegroundWindow(ctx->hwnd); return; }
    Context context; context.cfg=current; context.apply=onChanged; context.onCapture=onCapture; context.owner=parent;
    context.snapshots=snapshotDirectory; ctx=&context;
    HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc=WindowProc; wc.hInstance=instance;
    wc.hIcon=reinterpret_cast<HICON>(GetClassLongPtrW(parent,GCLP_HICON));
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName=L"MouseLockGame.Settings.v2";
    RegisterClassW(&wc);
    context.theme.SetDpi(GetDpiForWindow(parent));
    RECT r=context.theme.R(0,0,700,676); DWORD style=WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&r,style,FALSE,WS_EX_CONTROLPARENT,context.theme.dpi);
    RECT parentRect{}; GetWindowRect(parent,&parentRect);
    HWND hwnd=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"MouseLock · 偏好设置",style,
        parentRect.left+(parentRect.right-parentRect.left-(r.right-r.left))/2,
        parentRect.top+(parentRect.bottom-parentRect.top-(r.bottom-r.top))/2,
        r.right-r.left,r.bottom-r.top,parent,nullptr,instance,nullptr);
    if(!hwnd) { ctx=nullptr; return; }
    EnableWindow(parent,FALSE); ShowWindow(hwnd,SW_SHOW); UpdateWindow(hwnd);
    SetFocus(GetDlgItem(hwnd,IDC_TAB_SHORTCUTS));
    if(!snapshotDirectory.empty()) SetTimer(hwnd,90,250,nullptr);
    MSG message{};
    while(IsWindow(hwnd))
    {
        int result=GetMessageW(&message,nullptr,0,0);
        if(result<=0) { if(result==0) PostQuitMessage(static_cast<int>(message.wParam)); break; }
        if(!IsDialogMessageW(hwnd,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    EndRecording(); ctx=nullptr;
    if(IsWindow(hwnd)) DestroyWindow(hwnd);
    if(IsWindow(parent) && !context.ownerExiting) { EnableWindow(parent,TRUE); SetActiveWindow(parent); }
}

#include "../config.h"
#include <windows.h>
#include <cassert>
#include <iostream>
#include <string>

void Write(const std::wstring& path,const std::string& text)
{
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE); DWORD written=0;
    assert(WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr));
    assert(written==text.size()); CloseHandle(file);
}
int wmain(int argc,wchar_t** argv)
{
    assert(argc==2); std::wstring folder=argv[1]; CreateDirectoryW(folder.c_str(),nullptr);
    auto json=folder+L"\\config-test.json", legacy=folder+L"\\legacy-test.ini";
    auto defaults=LoadConfig(folder+L"\\missing.json");
    assert(defaults.exit.key==VK_F9 && defaults.exitHoldMs==2000);
    assert(defaults.toggle.key==VK_F12 && defaults.unlock.key==VK_F10);
    std::wstring error;
    assert(ValidateConfig(defaults,error));

    Write(legacy,"version=1\nenabled=0\nmode=manual\nsilent=1\nauto_start=0\nmonitor=\\\\.\\DISPLAY1\nprocesses=VALORANT-Win64-Shipping.exe;VALORANT.exe\nhotkey0=0,123\nhotkey1=0,121\n");
    AppConfig imported=ImportLegacyConfig(legacy);
    assert(!imported.enabled && imported.startSilent && !imported.autoStart && !imported.gameOnly);
    assert(imported.monitor==L"\\\\.\\DISPLAY1");
    assert(imported.exit.key==VK_F9 && imported.toggle.key==VK_F12 && imported.unlock.key==VK_F10);
    assert(ProcessNames(imported.processes).size()==2);
    assert(SaveConfig(json,imported));
    auto loaded=LoadConfig(json);
    assert(loaded.monitor==imported.monitor && loaded.startSilent);
    assert(loaded.processes==imported.processes && loaded.exitHoldMs==2000);

    loaded.exit={{Modifier::Control,Modifier::Shift},VK_F8}; loaded.exitHoldMs=5000;
    loaded.gameOnly=true; loaded.processes=L"game.exe;测试.exe";
    assert(SaveConfig(json,loaded));
    auto roundtrip=LoadConfig(json);
    assert(roundtrip.exitHoldMs==5000 && roundtrip.gameOnly);
    assert(FormatHotkey(roundtrip.exit)==L"Ctrl + Shift + F8");
    assert(roundtrip.processes==loaded.processes);
    roundtrip.exit={{},-1}; assert(SaveConfig(json,roundtrip));
    assert(!IsSet(LoadConfig(json).exit));

    Write(json,"{\"UnlockHotkey\":{\"Modifiers\":[\"Control\"],\"Key\":0},\"StartSilent\":true}");
    loaded=LoadConfig(json);
    assert(loaded.exit.key==VK_F9 && loaded.exitHoldMs==2000);
    assert(FormatHotkey(loaded.unlock)==L"Ctrl" && loaded.startSilent);
    Write(json,"{\"ExitHoldMs\":1}"); assert(LoadConfig(json).exitHoldMs==2000);
    Write(json,"{\"ExitHoldMs\":99999999999999999999999}"); assert(LoadConfig(json).exitHoldMs==2000);
    Write(json,"{\"ExitHotkey\":null,\"ExitHoldMs\":3000}");
    loaded=LoadConfig(json); assert(!IsSet(loaded.exit) && loaded.exitHoldMs==3000);

    Write(legacy,"enabled=1\r\nmode=game\r\nhotkey0= 3 , 65\r\nhotkey1=4,121\r\nprocesses=game.exe");
    imported=ImportLegacyConfig(legacy); // CRLF, spacing, modifier bits and no final newline
    assert(imported.enabled && imported.gameOnly && imported.processes==L"game.exe");
    assert(FormatHotkey(imported.toggle)==L"Ctrl + Alt + A" && FormatHotkey(imported.unlock)==L"Shift + F10");
    for(const char* invalid:{"hotkey0=8,65\nhotkey1=0,256\n","hotkey0=-1,65\nhotkey1=99999999999,121\n",
                             "hotkey0=0;65\nhotkey1=,121\n","hotkey0=\nhotkey1=2\n","hotkey0=a,65\nhotkey1=0,b\n"})
    {
        Write(legacy,invalid); imported=ImportLegacyConfig(legacy);
        assert(imported.toggle.key==VK_F12 && imported.unlock.key==VK_F10 && imported.exit.key==VK_F9);
    }
    Write(legacy,"hotkey0=0,0\nhotkey1=0,120\n"); // F9 was already bound, so the new exit binding stays empty
    imported=ImportLegacyConfig(legacy);
    assert(!IsSet(imported.toggle) && imported.unlock.key==VK_F9 && !IsSet(imported.exit));

    AppConfig duplicate; duplicate.exit=duplicate.toggle;
    assert(!ValidateConfig(duplicate,error));
    duplicate=AppConfig{}; duplicate.exit={{Modifier::Control,Modifier::Alt},VK_F12};
    assert(!ValidateConfig(duplicate,error));
    duplicate=AppConfig{}; duplicate.exitHoldMs=0; assert(!ValidateConfig(duplicate,error));
    duplicate=AppConfig{}; duplicate.processes=L"C:\\game.exe"; assert(!ValidateConfig(duplicate,error));
    assert(!SaveConfig(folder+L"\\nonexistent\\config.json",defaults));
    assert(ProcessNames(L"  one.exe ; two.exe\nthree.exe ").size()==3);
    DeleteFileW(json.c_str()); DeleteFileW(legacy.c_str());
    std::cout << "Config: defaults, legacy migration, Unicode round-trip, disabled exit, validation and failed save passed\n";
    return 0;
}

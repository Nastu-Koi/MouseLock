// 配置模块实现：手写 JSON 解析/序列化 + 快捷键格式化。
#include "config.h"

#include <windows.h>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <cwctype>

// ---------------- 极简 JSON DOM ----------------

struct JsonValue;

using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue
{
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    long long num = 0;
    std::string str;
    JsonArray arr;
    JsonObject obj;
};

namespace
{
    std::wstring Wide(const std::string& text)
    {
        int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
        return result;
    }
    std::string Utf8(const std::wstring& text)
    {
        int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
        return result;
    }
    std::string ReadText(const std::wstring& path)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) return {};
        DWORD size = GetFileSize(file, nullptr);
        if (size == INVALID_FILE_SIZE || size > 1024*1024) { CloseHandle(file); return {}; }
        std::string text(size, '\0');
        DWORD read = 0;
        bool ok = ReadFile(file, text.data(), size, &read, nullptr) != FALSE;
        CloseHandle(file);
        if (!ok) return {};
        text.resize(read);
        if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
        return text;
    }
    // ---------------- 解析 ----------------

    struct Parser
    {
        const std::string& s;
        size_t i = 0;

        void skipWs()
        {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
                ++i;
        }

        bool parseValue(JsonValue& out)
        {
            skipWs();
            if (i >= s.size())
                return false;
            char c = s[i];
            if (c == '{') return parseObject(out);
            if (c == '[') return parseArray(out);
            if (c == '"') { out.type = JsonValue::Type::String; return parseString(out.str); }
            if (c == 't' || c == 'f') return parseBool(out);
            if (c == 'n') return parseNull(out);
            if (c == '-' || (c >= '0' && c <= '9')) { out.type = JsonValue::Type::Number; return parseNumber(out.num); }
            return false;
        }

        bool parseObject(JsonValue& out)
        {
            out.type = JsonValue::Type::Object;
            out.obj.clear();
            ++i; // '{'
            skipWs();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            while (true)
            {
                skipWs();
                std::string key;
                if (i >= s.size() || s[i] != '"') return false;
                if (!parseString(key)) return false;
                skipWs();
                if (i >= s.size() || s[i] != ':') return false;
                ++i;
                JsonValue v;
                if (!parseValue(v)) return false;
                out.obj[key] = std::move(v);
                skipWs();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return false;
            }
        }

        bool parseArray(JsonValue& out)
        {
            out.type = JsonValue::Type::Array;
            out.arr.clear();
            ++i; // '['
            skipWs();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            while (true)
            {
                JsonValue v;
                if (!parseValue(v)) return false;
                out.arr.push_back(std::move(v));
                skipWs();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return false;
            }
        }

        bool parseString(std::string& out)
        {
            ++i; // '"'
            out.clear();
            while (i < s.size())
            {
                char c = s[i];
                if (c == '"') { ++i; return true; }
                if (c == '\\')
                {
                    ++i;
                    if (i >= s.size()) return false;
                    char e = s[i++];
                    switch (e)
                    {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        // \uXXXX —— 仅解码 ASCII/BMP，足够本配置使用
                        if (i + 4 > s.size()) return false;
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k)
                        {
                            char h = s[i++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else return false;
                        }
                        // 按 UTF-8 编码写入
                        if (cp < 0x80) out += char(cp);
                        else if (cp < 0x800)
                        {
                            out += char(0xC0 | (cp >> 6));
                            out += char(0x80 | (cp & 0x3F));
                        }
                        else
                        {
                            out += char(0xE0 | (cp >> 12));
                            out += char(0x80 | ((cp >> 6) & 0x3F));
                            out += char(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: return false;
                    }
                }
                else
                {
                    out += c;
                    ++i;
                }
            }
            return false;
        }

        bool parseBool(JsonValue& out)
        {
            if (s.compare(i, 4, "true") == 0) { out.type = JsonValue::Type::Bool; out.b = true; i += 4; return true; }
            if (s.compare(i, 5, "false") == 0) { out.type = JsonValue::Type::Bool; out.b = false; i += 5; return true; }
            return false;
        }

        bool parseNull(JsonValue& out)
        {
            if (s.compare(i, 4, "null") == 0) { out.type = JsonValue::Type::Null; i += 4; return true; }
            return false;
        }

        bool parseNumber(long long& out)
        {
            bool neg = false;
            if (i < s.size() && s[i] == '-') { neg = true; ++i; }
            long long v = 0;
            bool any = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            {
                int digit = s[i] - '0';
                if (v > (std::numeric_limits<long long>::max() - digit) / 10) return false;
                v = v * 10 + digit; ++i; any = true;
            }
            if (!any) return false;
            out = neg ? -v : v;
            return true;
        }
    };

    // ---------------- 序列化 ----------------

    void writeString(std::string& out, const std::string& s)
    {
        out += '"';
        for (char c : s)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
            }
        }
        out += '"';
    }

    void writeValue(std::string& out, const JsonValue& v, int indent);

    void writeIndent(std::string& out, int indent)
    {
        for (int k = 0; k < indent; ++k)
            out += ' ';
    }

    void writeObject(std::string& out, const JsonObject& obj, int indent)
    {
        if (obj.empty()) { out += "{}"; return; }
        out += "{\n";
        size_t n = obj.size();
        size_t idx = 0;
        for (const auto& [k, v] : obj)
        {
            writeIndent(out, indent + 2);
            writeString(out, k);
            out += ": ";
            writeValue(out, v, indent + 2);
            if (++idx < n) out += ",";
            out += "\n";
        }
        writeIndent(out, indent);
        out += "}";
    }

    void writeArray(std::string& out, const JsonArray& arr, int indent)
    {
        if (arr.empty()) { out += "[]"; return; }
        out += "[\n";
        size_t idx = 0;
        for (const auto& v : arr)
        {
            writeIndent(out, indent + 2);
            writeValue(out, v, indent + 2);
            if (++idx < arr.size()) out += ",";
            out += "\n";
        }
        writeIndent(out, indent);
        out += "]";
    }

    void writeValue(std::string& out, const JsonValue& v, int indent)
    {
        switch (v.type)
        {
        case JsonValue::Type::Null: out += "null"; break;
        case JsonValue::Type::Bool: out += v.b ? "true" : "false"; break;
        case JsonValue::Type::Number: out += std::to_string(v.num); break;
        case JsonValue::Type::String: writeString(out, v.str); break;
        case JsonValue::Type::Array: writeArray(out, v.arr, indent); break;
        case JsonValue::Type::Object: writeObject(out, v.obj, indent); break;
        }
    }

    // ---------------- 取值辅助 ----------------

    const JsonValue* find(const JsonObject& o, const char* key)
    {
        auto it = o.find(key);
        return it == o.end() ? nullptr : &it->second;
    }

    Modifier modifierFromString(const std::string& s)
    {
        if (s == "Alt") return Modifier::Alt;
        if (s == "Shift") return Modifier::Shift;
        return Modifier::Control; // 默认，也覆盖 "Control"
    }

    bool parseHotkey(const JsonValue* v, Hotkey& out)
    {
        if (!v || v->type != JsonValue::Type::Object)
            return false;
        const JsonValue* mods = find(v->obj, "Modifiers");
        out.modifiers.clear();
        if (mods && mods->type == JsonValue::Type::Array)
        {
            for (const auto& m : mods->arr)
                if (m.type == JsonValue::Type::String)
                    out.modifiers.push_back(modifierFromString(m.str));
        }
        const JsonValue* key = find(v->obj, "Key");
        if (key && key->type == JsonValue::Type::Number && (key->num < -1 || key->num > 255)) return false;
        out.key = (key && key->type == JsonValue::Type::Number) ? (int)key->num : 0;
        if (out.key == 0 && out.modifiers.empty()) out.key = -1;
        return true;
    }

    JsonValue hotkeyToJson(const Hotkey& hk)
    {
        JsonValue obj;
        obj.type = JsonValue::Type::Object;
        obj.obj["Modifiers"].type = JsonValue::Type::Array;
        for (auto m : hk.modifiers)
        {
            JsonValue s;
            s.type = JsonValue::Type::String;
            s.str = (m == Modifier::Alt) ? "Alt" : (m == Modifier::Shift) ? "Shift" : "Control";
            obj.obj["Modifiers"].arr.push_back(std::move(s));
        }
        obj.obj["Key"].type = JsonValue::Type::Number;
        obj.obj["Key"].num = hk.key;
        return obj;
    }

    // ---------------- 快捷键格式化 ----------------

    std::wstring modifierText(Modifier m)
    {
        switch (m)
        {
        case Modifier::Control: return L"Ctrl";
        case Modifier::Alt: return L"Alt";
        case Modifier::Shift: return L"Shift";
        }
        return L"";
    }

    std::wstring vkToText(int vk)
    {
        switch (vk)
        {
        case 0x25: return L"←";
        case 0x27: return L"→";
        case 0x26: return L"↑";
        case 0x28: return L"↓";
        case 0x20: return L"空格";
        case 0x09: return L"Tab";
        case 0x0D: return L"Enter";
        case 0x1B: return L"Esc";
        case 0x08: return L"Backspace";
        case 0x2E: return L"Delete";
        case 0x2D: return L"Insert";
        case 0x24: return L"Home";
        case 0x23: return L"End";
        case 0x21: return L"PageUp";
        case 0x22: return L"PageDown";
        }
        if (vk >= 0x30 && vk <= 0x39) return std::wstring(1, wchar_t(vk));
        if (vk >= 0x41 && vk <= 0x5A) return std::wstring(1, wchar_t(vk));
        if (vk >= 0x70 && vk <= 0x87) return L"F" + std::to_wstring(vk - 0x6F);
        // 其余可打印键（标点、小键盘等）：按当前键盘布局映射为字符
        UINT c = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_CHAR) & 0xFFFF;
        if (c >= 0x20 && c < 0xD800)
            return std::wstring(1, (wchar_t)c);
        return L"键 " + std::to_wstring(vk);
    }

    // MouseLockGame 1.x wrote hotkeys as "modifiers,key". Parsed by hand to keep
    // <sstream> out: it more than doubles the statically linked executable.
    bool parseLegacyHotkey(const std::string& value, Hotkey& out)
    {
        const char* text = value.c_str();
        char* end = nullptr;
        long modifiers = std::strtol(text, &end, 10);
        if (end == text) return false;
        while (std::isspace(static_cast<unsigned char>(*end))) ++end;
        if (*end != ',') return false;
        text = end + 1;
        long key = std::strtol(text, &end, 10);
        if (end == text || modifiers < 0 || modifiers > 7 || key < 0 || key > 255) return false;
        out.modifiers.clear();
        out.key = key == 0 && modifiers == 0 ? -1 : static_cast<int>(key);
        if (modifiers & MOD_CONTROL) out.modifiers.push_back(Modifier::Control);
        if (modifiers & MOD_ALT) out.modifiers.push_back(Modifier::Alt);
        if (modifiers & MOD_SHIFT) out.modifiers.push_back(Modifier::Shift);
        return true;
    }
}

// ---------------- 对外接口 ----------------

AppConfig LoadConfig(const std::wstring& path)
{
    AppConfig cfg;
    std::string text = ReadText(path);

    JsonValue root;
    Parser p{ text };
    if (!p.parseValue(root) || root.type != JsonValue::Type::Object)
        return cfg;

    if (const JsonValue* v = find(root.obj, "UnlockHotkey"))
    {
        if (v->type == JsonValue::Type::Null)
            cfg.unlock.key = -1; // 标记为“未设置”
        else
            parseHotkey(v, cfg.unlock);
    }

    if (const JsonValue* v = find(root.obj, "JumpLeft"))
    {
        if (v->type == JsonValue::Type::Null)
            cfg.jumpLeft.key = -1;
        else
            parseHotkey(v, cfg.jumpLeft);
    }

    if (const JsonValue* v = find(root.obj, "JumpRight"))
    {
        if (v->type == JsonValue::Type::Null)
            cfg.jumpRight.key = -1;
        else
            parseHotkey(v, cfg.jumpRight);
    }

    if (const JsonValue* v = find(root.obj, "StartSilent"))
        if (v->type == JsonValue::Type::Bool)
            cfg.startSilent = v->b;
    if (const JsonValue* v = find(root.obj, "AutoStart"))
        if (v->type == JsonValue::Type::Bool)
            cfg.autoStart = v->b;

    for (auto entry : {std::make_pair("ToggleHotkey", &cfg.toggle), std::make_pair("ExitHotkey", &cfg.exit)})
        if (const auto* value = find(root.obj, entry.first))
        {
            if (value->type == JsonValue::Type::Null) entry.second->key = -1;
            else parseHotkey(value, *entry.second);
        }
    if (const auto* value = find(root.obj, "ExitHoldMs"))
        if (value->type == JsonValue::Type::Number && value->num >= 1000 && value->num <= 10000)
            cfg.exitHoldMs = static_cast<unsigned>(value->num);
    if (const auto* value = find(root.obj, "Enabled"))
        if (value->type == JsonValue::Type::Bool) cfg.enabled = value->b;
    if (const auto* value = find(root.obj, "GameOnly"))
        if (value->type == JsonValue::Type::Bool) cfg.gameOnly = value->b;
    if (const auto* value = find(root.obj, "Monitor"))
        if (value->type == JsonValue::Type::String) cfg.monitor = Wide(value->str);
    if (const auto* value = find(root.obj, "Processes"))
        if (value->type == JsonValue::Type::String) cfg.processes = Wide(value->str);

    std::wstring error;
    if (!ValidateConfig(cfg, error)) return AppConfig{};

    return cfg;
}

bool SaveConfig(const std::wstring& path, const AppConfig& cfg)
{
    JsonValue root;
    root.type = JsonValue::Type::Object;

    auto makeHotkey = [](const Hotkey* hk) -> JsonValue {
        if (!hk)
        {
            JsonValue n; n.type = JsonValue::Type::Null; return n;
        }
        return hotkeyToJson(*hk);
    };

    root.obj["UnlockHotkey"] = cfg.unlock.key == -1 ? makeHotkey(nullptr) : hotkeyToJson(cfg.unlock);
    root.obj["JumpLeft"] = cfg.jumpLeft.key == -1 ? makeHotkey(nullptr) : hotkeyToJson(cfg.jumpLeft);
    root.obj["JumpRight"] = cfg.jumpRight.key == -1 ? makeHotkey(nullptr) : hotkeyToJson(cfg.jumpRight);
    root.obj["StartSilent"].type = JsonValue::Type::Bool;
    root.obj["StartSilent"].b = cfg.startSilent;
    root.obj["AutoStart"].type = JsonValue::Type::Bool;
    root.obj["AutoStart"].b = cfg.autoStart;
    root.obj["ToggleHotkey"] = IsSet(cfg.toggle) ? hotkeyToJson(cfg.toggle) : makeHotkey(nullptr);
    root.obj["ExitHotkey"] = IsSet(cfg.exit) ? hotkeyToJson(cfg.exit) : makeHotkey(nullptr);
    root.obj["ExitHoldMs"].type = JsonValue::Type::Number;
    root.obj["ExitHoldMs"].num = cfg.exitHoldMs;
    root.obj["Enabled"].type = JsonValue::Type::Bool;
    root.obj["Enabled"].b = cfg.enabled;
    root.obj["GameOnly"].type = JsonValue::Type::Bool;
    root.obj["GameOnly"].b = cfg.gameOnly;
    root.obj["Monitor"].type = JsonValue::Type::String;
    root.obj["Monitor"].str = Utf8(cfg.monitor);
    root.obj["Processes"].type = JsonValue::Type::String;
    root.obj["Processes"].str = Utf8(cfg.processes);

    std::string out;
    writeValue(out, root, 0);

    std::wstring temporary = path + L".tmp";
    HANDLE h = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        bool ok = WriteFile(h, out.data(), (DWORD)out.size(), &written, nullptr) && written == out.size();
        if (ok) ok = FlushFileBuffers(h) != FALSE;
        CloseHandle(h);
        if (ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        DeleteFileW(temporary.c_str());
    }
    return false;
}

AppConfig ImportLegacyConfig(const std::wstring& path)
{
    AppConfig cfg;
    const std::string text = ReadText(path);
    for (size_t start = 0; start < text.size();)
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        auto name = line.substr(0, equals), value = line.substr(equals + 1);
        if (name == "enabled") cfg.enabled = value == "1";
        if (name == "mode") cfg.gameOnly = value == "game";
        if (name == "silent") cfg.startSilent = value == "1";
        if (name == "auto_start") cfg.autoStart = value == "1";
        if (name == "monitor") cfg.monitor = Wide(value);
        if (name == "processes") cfg.processes = Wide(value);
        if (name == "hotkey0") parseLegacyHotkey(value, cfg.toggle);
        if (name == "hotkey1") parseLegacyHotkey(value, cfg.unlock);
    }
    // The new default must never invalidate an existing F9 binding.
    if (FormatHotkey(cfg.exit) == FormatHotkey(cfg.toggle) || FormatHotkey(cfg.exit) == FormatHotkey(cfg.unlock)) cfg.exit.key = -1;
    std::wstring error;
    if (!ValidateConfig(cfg, error)) return AppConfig{};
    return cfg;
}

std::vector<std::wstring> ProcessNames(const std::wstring& text)
{
    std::vector<std::wstring> result;
    std::wstring part;
    auto add = [&] {
        auto first = part.find_first_not_of(L" \t\r\n"), last = part.find_last_not_of(L" \t\r\n");
        if (first != std::wstring::npos) result.push_back(part.substr(first, last-first+1));
        part.clear();
    };
    for (wchar_t c : text) { if (c == L';' || c == L'\n' || c == L',') add(); else part += c; }
    add(); return result;
}

bool ValidateConfig(const AppConfig& cfg, std::wstring& error)
{
    std::vector<std::wstring> used;
    for (auto* hk : {&cfg.toggle, &cfg.unlock, &cfg.exit, &cfg.jumpLeft, &cfg.jumpRight})
    {
        if (!IsSet(*hk)) continue;
        auto text = FormatHotkey(*hk);
        if (hk->key < 0 || hk->key > 255 || text.empty()) { error = L"请选择有效的快捷键。"; return false; }
        if (text == L"Ctrl + Alt + F12") { error = L"Ctrl + Alt + F12 保留用于紧急解除锁定。"; return false; }
        if (std::find(used.begin(), used.end(), text) != used.end()) { error = L"不同功能不能使用相同的快捷键。"; return false; }
        used.push_back(text);
    }
    if ((IsSet(cfg.toggle) && cfg.toggle.key == 0) || (IsSet(cfg.jumpLeft) && cfg.jumpLeft.key == 0) || (IsSet(cfg.jumpRight) && cfg.jumpRight.key == 0))
    { error = L"切换锁定和切换屏幕需要一个主键。"; return false; }
    if (cfg.exitHoldMs < 1000 || cfg.exitHoldMs > 10000) { error = L"长按时长应在 1 到 10 秒之间。"; return false; }
    for (const auto& name : ProcessNames(cfg.processes))
    {
        if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos || name.size() < 5 || _wcsicmp(name.substr(name.size()-4).c_str(), L".exe") != 0)
        { error = L"请填写进程文件名（如 game.exe），多个名称用分号分隔。"; return false; }
    }
    if (cfg.gameOnly && ProcessNames(cfg.processes).empty()) { error = L"请先填写自动锁定的游戏进程。"; return false; }
    return true;
}

std::wstring FormatHotkey(const Hotkey& hk)
{
    if (!IsSet(hk))
        return L"";
    std::wstring res;
    bool first = true;
    auto addPart = [&](const std::wstring& p) {
        if (!first) res += L" + ";
        res += p;
        first = false;
    };
    static const Modifier order[] = { Modifier::Control, Modifier::Alt, Modifier::Shift };
    for (auto m : order)
    {
        for (auto h : hk.modifiers)
            if (h == m) { addPart(modifierText(m)); break; }
    }
    if (hk.key != 0)
    {
        auto t = vkToText(hk.key);
        if (!t.empty())
            addPart(t);
    }
    return res;
}

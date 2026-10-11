#pragma once
#include <windows.h>
#include <cstdio>
#include <string>
#include <string_view>

inline std::wstring widen(std::string_view s, UINT cp = CP_UTF8) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

inline std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Zawartość literału JSON (bez cudzysłowów).
inline std::wstring jsonEscape(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size() + s.size() / 8);
    for (wchar_t c : s) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf(buf, 8, L"\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

inline std::wstring jsonStr(std::wstring_view s) { return L"\"" + jsonEscape(s) + L"\""; }

inline std::wstring colorHex(COLORREF c) {
    wchar_t buf[8];
    swprintf(buf, 8, L"#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

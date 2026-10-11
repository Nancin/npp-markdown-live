#include "i18n.h"
#include <cwctype>
#include "Notepad_plus_msgs.h"
#include "text.h"

namespace {

std::string readFile(const std::wstring& path, DWORD limit = 1 << 20) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string data(limit, '\0');
    DWORD read = 0;
    ReadFile(h, data.data(), limit, &read, nullptr);
    CloseHandle(h);
    data.resize(read);
    return data;
}

// Płaski obiekt JSON z wartościami tekstowymi: { "klucz": "tekst", ... }.
class FlatJson {
public:
    explicit FlatJson(const std::wstring& s) : s_(s) {}

    bool parse(std::unordered_map<std::wstring, std::wstring>& out) {
        skip();
        if (!eat(L'{')) return false;
        skip();
        if (eat(L'}')) return true;
        for (;;) {
            std::wstring key, value;
            skip();
            if (!str(key)) return false;
            skip();
            if (!eat(L':')) return false;
            skip();
            if (!str(value)) return false;
            out[key] = value;
            skip();
            if (eat(L'}')) return true;
            if (!eat(L',')) return false;
        }
    }

private:
    void skip() { while (i_ < s_.size() && iswspace(s_[i_])) i_++; }
    bool eat(wchar_t c) { if (i_ < s_.size() && s_[i_] == c) { i_++; return true; } return false; }

    bool str(std::wstring& out) {
        if (!eat(L'"')) return false;
        while (i_ < s_.size()) {
            wchar_t c = s_[i_++];
            if (c == L'"') return true;
            if (c != L'\\') { out += c; continue; }
            if (i_ >= s_.size()) return false;
            wchar_t e = s_[i_++];
            switch (e) {
                case L'n': out += L'\n'; break;
                case L't': out += L'\t'; break;
                case L'r': out += L'\r'; break;
                case L'b': out += L'\b'; break;
                case L'f': out += L'\f'; break;
                case L'u':
                    if (i_ + 4 > s_.size()) return false;
                    out += (wchar_t)wcstoul(s_.substr(i_, 4).c_str(), nullptr, 16);
                    i_ += 4;
                    break;
                default: out += e;
            }
        }
        return false;
    }

    const std::wstring& s_;
    size_t i_ = 0;
};

std::wstring baseName(std::wstring file) {
    size_t sep = file.find_last_of(L"\\/");
    if (sep != std::wstring::npos) file = file.substr(sep + 1);
    if (file.size() > 4 && _wcsicmp(file.c_str() + file.size() - 4, L".xml") == 0) file.resize(file.size() - 4);
    return file;
}

} // namespace

void I18n::load(const std::wstring& dir, const std::wstring& lang) {
    lang_ = lang;
    strings_.clear();
    json_ = L"{}";
    if (lang.empty() || lang == L"english" || lang == L"english_customizable") return;

    std::string raw = readFile(dir + L"\\" + lang + L".json");
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) raw.erase(0, 3);   // BOM
    std::wstring text = widen(raw);
    std::unordered_map<std::wstring, std::wstring> parsed;
    if (FlatJson(text).parse(parsed)) {
        strings_ = std::move(parsed);
        json_ = text;
    }
}

const wchar_t* I18n::get(const wchar_t* key, const wchar_t* english) const {
    auto it = strings_.find(key);
    return it != strings_.end() && !it->second.empty() ? it->second.c_str() : english;
}

std::wstring detectNppLanguage(HWND npp, const std::wstring& iniPath, const std::wstring& nppConfigDir) {
    wchar_t forced[64] = {};
    GetPrivateProfileStringW(L"MarkdownLive", L"Language", L"", forced, 64, iniPath.c_str());
    std::wstring f = forced;
    if (f == L"pl") return L"polish";
    if (f == L"en") return L"english";
    if (!f.empty()) return f;

    char file[MAX_PATH] = {};
    if (SendMessageW(npp, NPPM_GETNATIVELANGFILENAME, MAX_PATH, (LPARAM)file) > 0) return baseName(widen(file));

    // Przed NPPN_READY Notepad++ może jeszcze nie odpowiadać: kopia wybranego tłumaczenia leży w jego katalogu ustawień.
    std::string head = readFile(nppConfigDir + L"\\nativeLang.xml", 4096);
    size_t at = head.find("filename=\"");
    if (at == std::string::npos) return L"english";
    at += 10;
    size_t end = head.find('"', at);
    return end == std::string::npos ? L"english" : baseName(widen(head.substr(at, end - at)));
}

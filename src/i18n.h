#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

// Teksty wtyczki z assets\i18n\<plik języka Notepad++ bez .xml>.json (np. german.json).
// Brak pliku albo klucza = tekst angielski podany w kodzie.
class I18n {
public:
    void load(const std::wstring& dir, const std::wstring& lang);
    const std::wstring& lang() const { return lang_; }
    const std::wstring& json() const { return json_; }
    const wchar_t* get(const wchar_t* key, const wchar_t* english) const;

private:
    std::wstring lang_, json_ = L"{}";
    std::unordered_map<std::wstring, std::wstring> strings_;
};

// Język Notepad++ (Ustawienia > Preferencje > Ogólne > Lokalizacja), np. "polish", "german", "english".
// Language=... w ustawieniach wtyczki wymusza wybór.
std::wstring detectNppLanguage(HWND npp, const std::wstring& iniPath, const std::wstring& nppConfigDir);

#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

// ----- Schowek -----

// Tekst (i opcjonalnie HTML w UTF-8, np. dla Worda i Outlooka).
bool setClipboard(HWND owner, const std::wstring& text, const std::string& html = {});
// Fragment HTML ze schowka (UTF-8), tylko gdy niesie formatowanie (linki, pogrubienia, listy, tabele...).
std::string clipboardHtml(HWND owner);
// Obraz bez tekstu (np. zrzut ekranu).
bool clipboardHasImageOnly();
bool saveClipboardImage(HWND owner, const std::wstring& path);
// Pliki skopiowane w Eksploratorze.
std::vector<std::wstring> clipboardFiles(HWND owner);
std::vector<std::wstring> dropFiles(HDROP drop);

// ----- Pliki -----

bool writeFile(const std::wstring& path, const std::string& bytes);
std::string readFile(const std::wstring& path, size_t max);
std::wstring saveDialog(HWND owner, const std::wstring& dir, const std::wstring& name, const wchar_t* filter, const wchar_t* ext);
// Pliki w folderze i podfolderach (względne, z ukośnikami), bez ukrytych i node_modules.
std::vector<std::wstring> listFiles(const std::wstring& dir, int depth, size_t max);
bool isImageFile(const std::wstring& path);
std::string base64Decode(const std::string& s);
std::wstring timestamp();   // 20261012-215301
std::string base64Encode(const std::string& s);
std::wstring imageMime(const std::wstring& path);

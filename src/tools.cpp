#include "tools.h"
#include <shellapi.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include "text.h"

// GDI+ (płaskie API) do zapisu obrazka ze schowka jako PNG.
extern "C" {
struct GdipStartupInput { UINT32 version; void* debugCallback; BOOL noBackgroundThread; BOOL noExternalCodecs; };
int WINAPI GdiplusStartup(ULONG_PTR* token, const GdipStartupInput* input, void* output);
void WINAPI GdiplusShutdown(ULONG_PTR token);
int WINAPI GdipCreateBitmapFromHBITMAP(HBITMAP bitmap, HPALETTE palette, void** out);
int WINAPI GdipSaveImageToFile(void* image, const WCHAR* file, const CLSID* encoder, const void* params);
int WINAPI GdipDisposeImage(void* image);
}

namespace {

UINT htmlFormat() {
    static UINT f = RegisterClipboardFormatW(L"HTML Format");
    return f;
}

size_t headerNumber(const std::string& s, const char* key) {
    size_t p = s.find(key);
    return p == std::string::npos ? std::string::npos : (size_t)strtoull(s.c_str() + p + strlen(key), nullptr, 10);
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

} // namespace

bool setClipboard(HWND owner, const std::wstring& text, const std::string& html) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    auto put = [](UINT format, const void* data, size_t bytes) {
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!mem) return;
        memcpy(GlobalLock(mem), data, bytes);
        GlobalUnlock(mem);
        if (!SetClipboardData(format, mem)) GlobalFree(mem);
    };
    put(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    if (!html.empty()) {
        // Format CF_HTML: nagłówek z przesunięciami (stała szerokość liczb), potem dokument z fragmentem.
        const char* head = "Version:0.9\r\nStartHTML:%010zu\r\nEndHTML:%010zu\r\nStartFragment:%010zu\r\nEndFragment:%010zu\r\n";
        std::string pre = "<html><head><meta charset=\"utf-8\"></head><body><!--StartFragment-->", post = "<!--EndFragment--></body></html>";
        char buf[160];
        size_t headLen = (size_t)snprintf(buf, sizeof buf, head, (size_t)0, (size_t)0, (size_t)0, (size_t)0);
        size_t startFrag = headLen + pre.size(), endFrag = startFrag + html.size();
        snprintf(buf, sizeof buf, head, headLen, endFrag + post.size(), startFrag, endFrag);
        std::string all = buf + pre + html + post;
        put(htmlFormat(), all.c_str(), all.size() + 1);
    }
    CloseClipboard();
    return true;
}

std::string clipboardHtml(HWND owner) {
    if (!IsClipboardFormatAvailable(htmlFormat()) || !OpenClipboard(owner)) return {};
    std::string raw;
    if (HANDLE h = GetClipboardData(htmlFormat())) {
        if (auto* p = static_cast<const char*>(GlobalLock(h))) {
            raw.assign(p, strnlen(p, GlobalSize(h)));
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    size_t a = headerNumber(raw, "StartFragment:"), b = headerNumber(raw, "EndFragment:");
    if (a == std::string::npos || b == std::string::npos || a >= b || b > raw.size()) return {};
    std::string frag = raw.substr(a, b - a), l = lower(frag);
    // Kod z edytorów (VS Code itp.) to same kolorowe <span> - zostaje zwykłym tekstem.
    for (const char* tag : {"<a ", "<b>", "<b ", "<strong", "<em", "<i>", "<i ", "<h1", "<h2", "<h3", "<h4", "<h5", "<h6", "<li", "<table", "<img",
                            "<blockquote", "<s>", "<del", "font-weight:700", "font-weight: 700", "font-weight:bold", "font-style:italic"}) {
        if (l.find(tag) != std::string::npos) return frag;
    }
    return {};
}

bool clipboardHasImageOnly() {
    return (IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB)) && !IsClipboardFormatAvailable(CF_UNICODETEXT) &&
           !IsClipboardFormatAvailable(CF_HDROP);
}

bool saveClipboardImage(HWND owner, const std::wstring& path) {
    static ULONG_PTR token = 0;
    if (!token) {
        GdipStartupInput in{1, nullptr, FALSE, FALSE};
        if (GdiplusStartup(&token, &in, nullptr) != 0) return false;
    }
    if (!OpenClipboard(owner)) return false;
    bool ok = false;
    if (HBITMAP bmp = (HBITMAP)GetClipboardData(CF_BITMAP)) {
        void* image = nullptr;
        if (GdipCreateBitmapFromHBITMAP(bmp, nullptr, &image) == 0 && image) {
            const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
            ok = GdipSaveImageToFile(image, path.c_str(), &png, nullptr) == 0;
            GdipDisposeImage(image);
        }
    }
    CloseClipboard();
    return ok;
}

std::vector<std::wstring> dropFiles(HDROP drop) {
    std::vector<std::wstring> out;
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; i++) {
        UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring f(len, L'\0');
        DragQueryFileW(drop, i, f.data(), len + 1);
        out.push_back(f);
    }
    return out;
}

std::vector<std::wstring> clipboardFiles(HWND owner) {
    std::vector<std::wstring> out;
    if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(owner)) return out;
    if (HANDLE h = GetClipboardData(CF_HDROP)) out = dropFiles((HDROP)h);
    CloseClipboard();
    return out;
}

bool writeFile(const std::wstring& path, const std::string& bytes) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD done = 0;
    bool ok = WriteFile(f, bytes.data(), (DWORD)bytes.size(), &done, nullptr) && done == bytes.size();
    CloseHandle(f);
    return ok;
}

std::string readFile(const std::wstring& path, size_t max) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string out(max, '\0');
    DWORD got = 0;
    ReadFile(f, out.data(), (DWORD)max, &got, nullptr);
    CloseHandle(f);
    out.resize(got);
    return out;
}

std::wstring saveDialog(HWND owner, const std::wstring& dir, const std::wstring& name, const wchar_t* filter, const wchar_t* ext) {
    wchar_t file[MAX_PATH * 2] = {};
    wcsncpy(file, name.c_str(), MAX_PATH * 2 - 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrDefExt = ext;
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&ofn) ? file : L"";
}

namespace {
void collect(const std::wstring& root, const std::wstring& rel, int depth, size_t max, std::vector<std::wstring>* out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\" + rel + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> dirs;
    do {
        std::wstring n = fd.cFileName;
        if (n[0] == L'.' || n == L"node_modules" || (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) dirs.push_back(rel + n + L"\\");
        else if (out->size() < max) out->push_back(rel + n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (depth > 0) for (auto& d : dirs) collect(root, d, depth - 1, max, out);
}
} // namespace

std::vector<std::wstring> listFiles(const std::wstring& dir, int depth, size_t max) {
    std::vector<std::wstring> out;
    if (dir.empty()) return out;
    collect(dir, L"", depth, max, &out);
    for (auto& f : out) std::replace(f.begin(), f.end(), L'\\', L'/');
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return out;
}

bool isImageFile(const std::wstring& path) {
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    for (const wchar_t* e : {L".png", L".jpg", L".jpeg", L".gif", L".webp", L".svg", L".bmp", L".ico", L".avif"}) {
        if (_wcsicmp(ext, e) == 0) return true;
    }
    return false;
}

std::string base64Decode(const std::string& s) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -8;
    for (char c : s) {
        size_t v = chars.find(c);
        if (v == std::string::npos) continue;
        val = (val << 6) | (int)v;
        bits += 6;
        if (bits >= 0) {
            out += (char)((val >> bits) & 0xff);
            bits -= 8;
        }
    }
    return out;
}

std::wstring timestamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
}

std::string base64Encode(const std::string& s) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((s.size() + 2) / 3 * 4);
    for (size_t i = 0; i < s.size(); i += 3) {
        unsigned v = (unsigned char)s[i] << 16;
        if (i + 1 < s.size()) v |= (unsigned char)s[i + 1] << 8;
        if (i + 2 < s.size()) v |= (unsigned char)s[i + 2];
        out += chars[(v >> 18) & 63];
        out += chars[(v >> 12) & 63];
        out += i + 1 < s.size() ? chars[(v >> 6) & 63] : '=';
        out += i + 2 < s.size() ? chars[v & 63] : '=';
    }
    return out;
}

std::wstring imageMime(const std::wstring& path) {
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    if (!_wcsicmp(ext, L".jpg") || !_wcsicmp(ext, L".jpeg")) return L"image/jpeg";
    if (!_wcsicmp(ext, L".svg")) return L"image/svg+xml";
    if (!_wcsicmp(ext, L".ico")) return L"image/x-icon";
    if (*ext) {
        std::wstring e = ext + 1;
        for (auto& c : e) c = (wchar_t)towlower(c);
        return L"image/" + e;
    }
    return L"application/octet-stream";
}

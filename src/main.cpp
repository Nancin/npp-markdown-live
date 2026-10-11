#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cwctype>
#include <climits>
#include "PluginInterface.h"
#include "Docking.h"
#include "detect.h"
#include "edit.h"
#include "i18n.h"
#include "layout.h"
#include "panel.h"
#include "text.h"
#include "sysprint.h"
#include "tools.h"

namespace {

enum Cmd { C_TOGGLE, C_REFRESH, C_FORCE, C_SEP, C_SYNC, C_AUTO, C_RESET, C_SEP2, C_COPY, C_EXPORT, C_TIDY, C_PRESENT, C_SEP3, C_ABOUT, C_COUNT };
enum Timer : UINT_PTR { T_RENDER = 1, T_LAYOUT, T_RESET, T_LIST, T_FOCUS, T_RENUMBER };

constexpr wchar_t kModule[] = L"MarkdownLive.dll";
constexpr wchar_t kIniSection[] = L"MarkdownLive";
constexpr size_t kMaxRenderBytes = 6 * 1024 * 1024;

struct Settings { bool dark = false, sync = true, autoOpen = true; int zoom = 100; };

HINSTANCE gInst;
NppData npp;
FuncItem funcs[C_COUNT];
ShortcutKey toggleKey{true, true, false, 'M'};   // Ctrl+Alt+M
Settings cfg;
Panel panel;
DockLayout layout;
std::wstring pluginDir, iniPath;
I18n i18n;

std::unordered_map<UINT_PTR, int> forced;   // bufor -> 1 wymuszony Markdown, 0 wymuszony zwykły tekst
std::unordered_set<UINT_PTR> autoTried;     // bufory, dla których podgląd już raz otwierano automatycznie
bool autoShown = false;                     // panel otworzył się sam (można go sam schować)
bool layoutBusy = false;
int resetTries = 0;
int lastMode = -1;                          // 0 pusty ekran, 1 wyrenderowany tekst
size_t lastHash = 0;
std::wstring lastDir;
ULONGLONG ignoreScrollUntil = 0;
long long renumberLine = LLONG_MAX;   // najwyższa linia ze zmianą liczby linii od ostatniego przenumerowania
bool renumberDeleted = false;         // czy wśród tych zmian było usunięcie linii
bool lastModUndo = false;             // ostatnia zmiana tekstu to cofnięcie / ponowienie (spis treści wtedy nie wraca)
HWND pasteTarget = nullptr;           // edytor czekający na HTML zamieniony na Markdown

bool listEditing();

const wchar_t* tr(const wchar_t* key, const wchar_t* english) { return i18n.get(key, english); }

LRESULT nppMsg(UINT msg, WPARAM w = 0, LPARAM l = 0) { return SendMessageW(npp._nppHandle, msg, w, l); }

HWND currentSci() {
    int v = 0;
    nppMsg(NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&v);
    return v == 1 ? npp._scintillaSecondHandle : npp._scintillaMainHandle;
}

LRESULT sci(HWND h, UINT msg, WPARAM w = 0, LPARAM l = 0) { return SendMessageW(h, msg, w, l); }

bool panelVisible() { return panel.hwnd() && IsWindowVisible(panel.hwnd()); }

void setCheck(Cmd c, bool on) { nppMsg(NPPM_SETMENUITEMCHECK, funcs[c]._cmdID, on); }

std::wstring pluginsConfigDir() {
    wchar_t buf[MAX_PATH] = {};
    nppMsg(NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)buf);
    return buf;
}

void saveSettings() {
    auto put = [](const wchar_t* key, bool v) { WritePrivateProfileStringW(kIniSection, key, v ? L"1" : L"0", iniPath.c_str()); };
    put(L"Dark", cfg.dark);
    put(L"SyncScroll", cfg.sync);
    put(L"AutoOpen", cfg.autoOpen);
    WritePrivateProfileStringW(kIniSection, L"Zoom", std::to_wstring(cfg.zoom).c_str(), iniPath.c_str());
}

// ---------- Dokument ----------

struct Doc {
    UINT_PTR id;
    std::wstring path;
    HWND sci;
    int lang;
    std::string_view bytes;
    UINT codepage;
};

Doc currentDoc() {
    Doc d{};
    d.sci = currentSci();
    d.id = (UINT_PTR)nppMsg(NPPM_GETCURRENTBUFFERID);
    wchar_t buf[MAX_PATH * 2] = {};
    nppMsg(NPPM_GETFULLPATHFROMBUFFERID, d.id, (LPARAM)buf);
    d.path = buf;
    int lang = 0;
    nppMsg(NPPM_GETCURRENTLANGTYPE, 0, (LPARAM)&lang);
    d.lang = lang;
    size_t len = (size_t)sci(d.sci, SCI_GETLENGTH);
    d.bytes = {(const char*)sci(d.sci, SCI_GETCHARACTERPOINTER), len};
    UINT cp = (UINT)sci(d.sci, SCI_GETCODEPAGE);
    d.codepage = cp == 0 ? CP_ACP : cp;
    return d;
}

std::wstring parentDir(const std::wstring& path) {
    size_t sep = path.find_last_of(L"\\/");
    return sep == std::wstring::npos ? L"" : path.substr(0, sep);
}

bool isMarkdown(const Doc& d) {
    auto it = forced.find(d.id);
    if (it != forced.end()) return it->second == 1;
    if (hasMarkdownExtension(d.path)) return true;
    if (d.lang != L_TEXT && d.lang != L_USER) return false;
    return d.bytes.size() <= kMaxRenderBytes && looksLikeMarkdown(d.bytes);
}

// ---------- Przewijanie ----------

std::wstring num(double v) {
    if (v < 0) v = 0;
    long long scaled = (long long)(v * 10000 + 0.5);
    wchar_t buf[40];
    swprintf(buf, 40, L"%lld.%04lld", scaled / 10000, scaled % 10000);
    return buf;
}

double parseNum(const std::wstring& s) {
    double v = 0, scale = 0.1;
    bool frac = false;
    for (wchar_t c : s) {
        if (c == L'.') frac = true;
        else if (c >= L'0' && c <= L'9') {
            if (frac) { v += (c - L'0') * scale; scale /= 10; }
            else v = v * 10 + (c - L'0');
        }
    }
    return v;
}

double displayToLine(HWND h, int first) {
    int doc = (int)sci(h, SCI_DOCLINEFROMVISIBLE, first);
    int base = (int)sci(h, SCI_VISIBLEFROMDOCLINE, doc);
    int wraps = std::max<int>(1, (int)sci(h, SCI_WRAPCOUNT, doc));
    return doc + (double)(first - base) / wraps;
}

int maxFirstLine(HWND h) {
    int last = (int)sci(h, SCI_GETLINECOUNT) - 1;
    int total = (int)sci(h, SCI_VISIBLEFROMDOCLINE, last) + std::max<int>(1, (int)sci(h, SCI_WRAPCOUNT, last));
    return std::max(0, total - (int)sci(h, SCI_LINESONSCREEN));
}

void sendScroll() {
    if (!cfg.sync || !panelVisible() || lastMode != 1 || GetTickCount64() < ignoreScrollUntil) return;
    HWND h = currentSci();
    int first = (int)sci(h, SCI_GETFIRSTVISIBLELINE);
    double line = displayToLine(h, first), lmax = displayToLine(h, maxFirstLine(h));
    bool end = first > 0 && first >= maxFirstLine(h);
    panel.post(L"scroll", L"{\"type\":\"scroll\",\"line\":" + num(line) + L",\"lmax\":" + num(lmax) +
                              L",\"end\":" + (end ? L"true" : L"false") + L"}");
}

void syncEditor(double line, bool end) {
    HWND h = currentSci();
    int first;
    if (end) {
        first = maxFirstLine(h);
    } else {
        int doc = (int)line;
        int wraps = std::max<int>(1, (int)sci(h, SCI_WRAPCOUNT, doc));
        first = (int)sci(h, SCI_VISIBLEFROMDOCLINE, doc) + (int)((line - doc) * wraps + 0.5);
    }
    ignoreScrollUntil = GetTickCount64() + 220;
    sci(h, SCI_SETFIRSTVISIBLELINE, first);
}

void gotoLine(int line) {
    HWND h = currentSci();
    ignoreScrollUntil = GetTickCount64() + 220;
    sci(h, SCI_ENSUREVISIBLE, line);
    sci(h, SCI_GOTOLINE, line);
    int top = (int)sci(h, SCI_VISIBLEFROMDOCLINE, line) - (int)sci(h, SCI_LINESONSCREEN) / 3;
    sci(h, SCI_SETFIRSTVISIBLELINE, std::max(0, top));
    SetFocus(h);
}

// Styl linii z kursorem (akapit, nagłówek, lista, cytat) dla przycisku „Styl” na pasku podglądu.
std::wstring lastBlock;
Sci_Position lastCaretLine = -1;

void sendCaretBlock(bool force = false) {
    if (!panelVisible()) return;
    HWND h = currentSci();
    Sci_Position line = sci(h, SCI_LINEFROMPOSITION, sci(h, SCI_GETCURRENTPOS));
    Sci_Position st = sci(h, SCI_POSITIONFROMLINE, line);
    Sci_Position len = std::min<Sci_Position>(sci(h, SCI_GETLINEENDPOSITION, line) - st, 24);
    std::string_view text(len > 0 ? (const char*)sci(h, SCI_GETRANGEPOINTER, st, len) : "", len > 0 ? (size_t)len : 0);
    while (!text.empty() && (text[0] == ' ' || text[0] == '\t')) text.remove_prefix(1);

    std::wstring block = L"p";
    size_t hashes = text.find_first_not_of('#');
    if (hashes >= 1 && hashes <= 6 && hashes < text.size() && text[hashes] == ' ') block = L"h" + std::to_wstring(hashes);
    else if (text.size() >= 2 && (text[0] == '-' || text[0] == '*' || text[0] == '+') && text[1] == ' ')
        block = text.size() >= 5 && text[2] == '[' && text[4] == ']' ? L"task" : L"ul";
    else if (!text.empty() && text[0] == '>') block = L"quote";
    else {
        size_t d = text.find_first_not_of("0123456789");
        if (d > 0 && d != std::string_view::npos && d + 1 < text.size() && (text[d] == '.' || text[d] == ')') && text[d + 1] == ' ') block = L"ol";
    }
    // Linia z kursorem także dla znacznika na marginesie podglądu.
    if (block == lastBlock && line == lastCaretLine && !force) return;
    lastBlock = block;
    lastCaretLine = line;
    panel.post(L"caret", L"{\"type\":\"caret\",\"block\":\"" + block + L"\",\"line\":" + std::to_wstring(line) + L"}");
}

// ---------- Wygląd ----------

void sendConfig() {
    COLORREF bg = cfg.dark ? RGB(0x1d, 0x21, 0x28) : RGB(0xf6, 0xf3, 0xec);
    COLORREF fg = cfg.dark ? RGB(0xd5, 0xda, 0xe2) : RGB(0x33, 0x31, 0x2d);
    panel.setBackground(bg);

    std::wstring json = L"{\"type\":\"config\",\"dark\":" + std::wstring(cfg.dark ? L"true" : L"false") +
        L",\"bg\":" + jsonStr(colorHex(bg)) + L",\"fg\":" + jsonStr(colorHex(fg)) +
        L",\"sync\":" + (cfg.sync ? L"true" : L"false") + L",\"zoom\":" + std::to_wstring(cfg.zoom) + L",\"lang\":" + jsonStr(i18n.lang()) +
        L",\"i18n\":" + i18n.json() + L"}";
    panel.post(L"config", json);
}

// ---------- Odświeżanie podglądu ----------

void showPanel() { nppMsg(NPPM_DMMSHOW, 0, (LPARAM)panel.hwnd()); }
void hidePanel() { nppMsg(NPPM_DMMHIDE, 0, (LPARAM)panel.hwnd()); }

void refresh(bool force = false, bool activation = false) {
    Doc d = currentDoc();
    bool md = isMarkdown(d);
    setCheck(C_FORCE, md);

    if (!panelVisible()) {
        if (cfg.autoOpen && md && !autoTried.count(d.id)) {
            autoTried.insert(d.id);
            autoShown = true;
            showPanel();
        }
        return;
    }
    if (!md && activation && autoShown) {
        autoShown = false;
        hidePanel();
        return;
    }
    if (!md) {
        if (lastMode != 0 || force) panel.post(L"view", L"{\"type\":\"empty\"}");
        lastMode = 0;
        return;
    }

    size_t hash = std::hash<std::string_view>{}(d.bytes) ^ ((size_t)d.codepage << 1);
    std::wstring dir = parentDir(d.path);
    if (!force && lastMode == 1 && hash == lastHash && dir == lastDir) return;
    lastMode = 1;
    lastHash = hash;
    lastDir = dir;

    panel.mapDocFolder(dir);
    int lines = (int)sci(d.sci, SCI_GETLINECOUNT);
    // Przy pisaniu (force == false) podgląd nie jest wyrównywany do edytora - tylko pokazuje edytowany blok,
    // gdy ten wyjdzie poza ekran. Pełna synchronizacja: zmiana karty, zapis, przewinięcie edytora.
    long long caret = force ? -1 : (long long)sci(d.sci, SCI_LINEFROMPOSITION, sci(d.sci, SCI_GETCURRENTPOS));
    panel.post(L"view", L"{\"type\":\"render\",\"text\":" + jsonStr(widen(d.bytes, d.codepage)) + L",\"lines\":" +
                            std::to_wstring(lines) + L",\"lmax\":" + num(displayToLine(d.sci, maxFirstLine(d.sci))) +
                            L",\"caret\":" + std::to_wstring(caret) + L"}");
    if (force) sendScroll();
    sendCaretBlock(true);
}

void scheduleRefresh(UINT ms = 120) { SetTimer(panel.hwnd(), T_RENDER, ms, nullptr); }

// ---------- Komendy menu ----------

void cmdToggle() {
    autoShown = false;
    if (panelVisible()) hidePanel(); else showPanel();
}

void cmdRefresh() { refresh(true); }

void cmdForce() {
    Doc d = currentDoc();
    forced[d.id] = isMarkdown(d) ? 0 : 1;
    autoTried.insert(d.id);
    lastMode = -1;
    if (panelVisible()) refresh(true); else if (forced[d.id]) { autoShown = false; showPanel(); } else refresh();
    setCheck(C_FORCE, forced[d.id] == 1);
}

void cmdSync() {
    cfg.sync = !cfg.sync;
    setCheck(C_SYNC, cfg.sync);
    saveSettings();
    sendConfig();
    sendScroll();
}

void cmdAuto() {
    cfg.autoOpen = !cfg.autoOpen;
    setCheck(C_AUTO, cfg.autoOpen);
    saveSettings();
}

void cmdReset() {
    layout.resetToHalf();
    layout.apply();
}

void cmdAbout() {
    std::wstring text = L"Markdown Live 1.0\n\n";
    text += tr(L"about", L"Markdown preview beside the editor: 50/50 layout, scroll sync and two-way double-click.");
    text += L"\n\nCtrl+Alt+M\nhttps://github.com/Nancin/npp-markdown-live";
    MessageBoxW(npp._nppHandle, text.c_str(), L"Markdown Live", MB_OK | MB_ICONINFORMATION);
}

// ---------- Komunikaty ze strony podglądu ----------

std::wstring printPdfPath, exportPath, lastSaved;
bool printing = false, printPreview = false, presentPending = false;

// Nazwa dokumentu bez rozszerzenia (dla nowych plików: "new 1").
std::wstring docBaseName() {
    Doc d = currentDoc();
    std::wstring name = d.path.substr(d.path.find_last_of(L"\\/") + 1);
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    return name;
}

std::wstring askSavePath(const wchar_t* ext, const wchar_t* filter, const std::wstring& base = L"") {
    return saveDialog(npp._nppHandle, parentDir(currentDoc().path), (base.empty() ? docBaseName() : base) + L"." + ext, filter, ext);
}

// Wydruk idzie przez PDF w folderze tymczasowym: okno podglądu wydruku pokazuje dokładnie te strony.
std::wstring previewPdfPath() {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"MarkdownLive";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    std::wstring name = docBaseName();
    for (auto& c : name) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    return dir + L"\\" + (name.empty() ? L"document" : name) + L".pdf";
}

// Komunikat w dymku podglądu: key = tekst z tłumaczeń, name = nazwa pliku, open = przycisk „Otwórz”.
void notice(const wchar_t* key, const std::wstring& name = L"", bool open = false) {
    panel.post(L"notice", L"{\"type\":\"notice\",\"key\":" + jsonStr(key) + L",\"name\":" + jsonStr(name) +
                              L",\"open\":" + (open ? L"true" : L"false") + L"}");
}

std::wstring fileName(const std::wstring& path) { return path.substr(path.find_last_of(L"\\/") + 1); }

// Linki do plików (z Eksploratora: przeciągnięte albo skopiowane); obrazki jako ![](...).
void insertFileLinks(HWND h, const std::vector<std::wstring>& files) {
    std::wstring dir = parentDir(currentDoc().path), out;
    for (const auto& f : files) {
        if (GetFileAttributesW(f.c_str()) & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fileName(f), path = markdownPath(f, dir);
        if (!out.empty()) out += L"\n";
        if (isImageFile(f)) {
            size_t dot = name.rfind(L'.');
            out += L"![" + (dot == std::wstring::npos ? name : name.substr(0, dot)) + L"](" + path + L")";
        } else {
            out += L"[" + name + L"](" + path + L")";
        }
    }
    insertText(h, out);
}

// Obrazek ze schowka: PNG w folderze images obok dokumentu (dla nowego dokumentu w Obrazach).
bool pasteImage(HWND h) {
    std::wstring dir = parentDir(currentDoc().path), folder;
    if (dir.empty()) {
        wchar_t pics[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, pics))) return false;
        folder = std::wstring(pics) + L"\\Markdown Live";
    } else {
        folder = dir + L"\\images";
    }
    SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    std::wstring base = dir.empty() ? L"image" : docBaseName();
    std::wstring path = folder + L"\\" + base + L"-" + timestamp() + L".png";
    for (int i = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; i++) {
        path = folder + L"\\" + base + L"-" + timestamp() + L"-" + std::to_wstring(i) + L".png";
    }
    if (!saveClipboardImage(npp._nppHandle, path)) return false;
    insertText(h, L"![" + base + L"](" + markdownPath(path, dir) + L")");
    return true;
}

// ---------- Podpowiedzi przy pisaniu (pliki, nagłówki, emoji) ----------

enum AcKind { AC_NONE, AC_PATH, AC_EMOJI };
AcKind acKind = AC_NONE;
std::wstring headingSlugs;   // z podglądu, rozdzielone \n
std::string emojiNames;      // "+1: 100: smile: ..." z biblioteki markdown-it-emoji

const std::string& emojiList() {
    if (!emojiNames.empty()) return emojiNames;
    std::string js = readFile(pluginDir + L"\\assets\\lib\\markdown-it-emoji.min.js", 1 << 20);
    size_t a = js.find("={100:");
    if (a == std::string::npos) return emojiNames;
    std::vector<std::string> names;
    for (size_t p = a + 2; p < js.size() && js[p] != '}';) {
        size_t colon = js.find(':', p), comma = js.find_first_of(",}", colon);
        if (colon == std::string::npos || comma == std::string::npos) break;
        std::string key = js.substr(p, colon - p);
        if (key.size() > 1 && key[0] == '"') key = key.substr(1, key.size() - 2);
        names.push_back(key + ":");
        p = js[comma] == ',' ? comma + 1 : comma;
    }
    std::sort(names.begin(), names.end());
    for (const auto& n : names) emojiNames += (emojiNames.empty() ? "" : " ") + n;
    return emojiNames;
}

void showList(HWND h, int entered, const std::string& items, AcKind kind) {
    if (items.empty()) return;
    sci(h, SCI_AUTOCSETIGNORECASE, 1);
    sci(h, SCI_AUTOCSETORDER, SC_ORDER_PERFORMSORT);
    sci(h, SCI_AUTOCSHOW, entered, (LPARAM)items.c_str());
    sci(h, SCI_AUTOCSETORDER, SC_ORDER_PRESORTED);   // kolejność liczy się tylko przy pokazaniu; Notepad++ ma swoją
    acKind = kind;
}

std::string toDoc(HWND h, const std::wstring& w) {
    UINT cp = (UINT)sci(h, SCI_GETCODEPAGE);
    if (cp == 0) cp = CP_ACP;
    int n = WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

void onCharForCompletion(HWND h, int ch) {
    Sci_Position pos = sci(h, SCI_GETCURRENTPOS);
    auto at = [&](Sci_Position p) { return p >= 0 ? (char)sci(h, SCI_GETCHARAT, p) : '\n'; };
    if ((ch == '(' && at(pos - 2) == ']') || (ch == '#' && at(pos - 2) == '(' && at(pos - 3) == ']')) {
        // ](  -> pliki z folderu dokumentu i #nagłówki;  ](#  -> same nagłówki
        std::wstring items;
        if (!headingSlugs.empty()) {
            size_t start = 0;
            for (size_t nl; start <= headingSlugs.size(); start = nl + 1) {
                nl = headingSlugs.find(L'\n', start);
                if (nl == std::wstring::npos) nl = headingSlugs.size();
                if (nl > start) items += (items.empty() ? L"" : L" ") + std::wstring(ch == '#' ? L"" : L"#") + headingSlugs.substr(start, nl - start);
            }
        }
        if (ch == '(') {
            std::wstring dir = parentDir(currentDoc().path);
            for (const auto& f : listFiles(dir, 2, 400)) {
                std::wstring enc = markdownPath(dir + L"\\" + f, dir);
                if (_wcsicmp(enc.c_str(), markdownPath(currentDoc().path, dir).c_str()) != 0) items += (items.empty() ? L"" : L" ") + enc;
            }
        }
        showList(h, 0, toDoc(h, items), AC_PATH);
        return;
    }
    // :sm -> emoji (dwie litery po dwukropku na początku słowa)
    if (ch < 128 && (isalnum(ch) || ch == '_' || ch == '+' || ch == '-')) {
        if (sci(h, SCI_AUTOCACTIVE) && acKind == AC_EMOJI) return;   // lista Notepad++ ustępuje emoji
        Sci_Position st = pos;
        while (st > 0 && pos - st < 3) {
            char c = at(st - 1);
            if (!(isalnum((unsigned char)c) || c == '_' || c == '+' || c == '-')) break;
            st--;
        }
        if (pos - st != 2 || at(st - 1) != ':') return;
        char before = at(st - 2);
        if (!(before == ' ' || before == '\t' || before == '\n' || before == '\r' || before == '(' || st - 1 == 0)) return;
        showList(h, 2, emojiList(), AC_EMOJI);
    }
}

// Po wybraniu pliku z listy domykamy nawias linku.
void onCompletionDone(HWND h) {
    if (acKind == AC_PATH) {
        Sci_Position pos = sci(h, SCI_GETCURRENTPOS);
        if ((char)sci(h, SCI_GETCHARAT, pos) != ')') {
            sci(h, SCI_INSERTTEXT, pos, (LPARAM)")");
            sci(h, SCI_GOTOPOS, pos + 1);
        }
    }
    acKind = AC_NONE;
}

// ---------- Polecenia menu (kopiowanie, eksport, porządki, prezentacja) ----------

void ensurePanel() {
    if (!panelVisible()) { autoShown = false; showPanel(); }
}

void cmdCopy() {
    ensurePanel();
    panel.post(L"copyrich", L"{\"type\":\"copy-rich\"}");
}

void cmdExport() {
    exportPath = askSavePath(L"html", L"HTML\0*.html;*.htm\0");
    if (exportPath.empty()) return;
    ensurePanel();
    panel.post(L"export", L"{\"type\":\"export-html\",\"title\":" + jsonStr(docBaseName()) + L"}");
}

void cmdTidy() { tidyDocument(currentSci()); }

void startPresentation() {
    if (!panel.ready()) { presentPending = true; ensurePanel(); return; }
    presentPending = false;
    panel.setFullscreen(true, npp._nppHandle);
    panel.post(L"present", L"{\"type\":\"present\",\"on\":true}");
}

void cmdPresent() {
    ensurePanel();
    startPresentation();
}

void endPresentation() {
    if (!panel.fullscreen()) return;
    panel.post(L"present", L"{\"type\":\"present\",\"on\":false}");
    panel.setFullscreen(false, npp._nppHandle);
}

// Brakujące pliki z linków i obrazków (adresy podglądu -> ścieżki na dysku).
std::wstring checkFiles(const std::wstring& urls);

// Adres z podglądu na ścieżkę pliku: mdlive.doc = folder dokumentu, drive-c.mdlive = dysk C:.
std::wstring localPath(const std::wstring& url) {
    std::wstring base, rest;
    const std::wstring doc = L"https://mdlive.doc/";
    if (url.rfind(doc, 0) == 0) {
        if (lastDir.empty()) return L"";
        base = lastDir + L"\\";
        rest = url.substr(doc.size());
    } else if (url.rfind(L"https://drive-", 0) == 0 && url.size() > 22 && url.compare(15, 8, L".mdlive/") == 0) {
        base = std::wstring(1, (wchar_t)towupper(url[14])) + L":\\";
        rest = url.substr(23);
    } else {
        return L"";
    }
    rest = rest.substr(0, rest.find_first_of(L"?#"));
    std::vector<wchar_t> decoded(rest.size() + 1);
    DWORD n = (DWORD)decoded.size();
    if (FAILED(UrlUnescapeW(const_cast<wchar_t*>(rest.c_str()), decoded.data(), &n, URL_UNESCAPE_AS_UTF8))) return L"";
    std::wstring path = base + decoded.data();
    for (auto& c : path) if (c == L'/') c = L'\\';
    return path;
}

void openLink(const std::wstring& url) {
    std::wstring path = localPath(url);
    if (!path.empty()) {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
        bool text = hasMarkdownExtension(path) || (path.size() > 4 && _wcsicmp(path.c_str() + path.size() - 4, L".txt") == 0);
        if (text) nppMsg(NPPM_DOOPEN, 0, (LPARAM)path.c_str());
        else ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0 || url.rfind(L"mailto:", 0) == 0) {
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// Stan plików z linków i obrazków: brakujące oraz czas zmiany istniejących (podgląd przeładowuje obrazek po zmianie pliku).
std::wstring checkFiles(const std::wstring& urls) {
    std::wstring missing, times;
    size_t start = 0;
    for (size_t nl; start < urls.size(); start = nl + 1) {
        nl = urls.find(L'\n', start);
        if (nl == std::wstring::npos) nl = urls.size();
        std::wstring url = urls.substr(start, nl - start), path = localPath(url);
        if (path.empty()) continue;
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) {
            missing += (missing.empty() ? L"" : L",") + jsonStr(url);
        } else {
            ULONGLONG t = (ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32 | fa.ftLastWriteTime.dwLowDateTime;
            times += (times.empty() ? L"" : L",") + jsonStr(url) + L":\"" + std::to_wstring(t / 10000) + L"\"";
        }
    }
    return L"\"missing\":[" + missing + L"],\"times\":{" + times + L"}";
}

// Adresy podglądu w HTML wychodzącym poza wtyczkę: obrazki wbudowane (data:) albo jako file:///, linki jako file:///.
std::wstring externalHtml(std::wstring html, bool embedImages) {
    if (embedImages) {
        for (size_t p = 0; (p = html.find(L"src=\"https://", p)) != std::wstring::npos;) {
            size_t a = p + 5, z = html.find(L'"', a);
            if (z == std::wstring::npos) break;
            std::wstring path = localPath(html.substr(a, z - a));
            std::string bytes = path.empty() ? std::string() : readFile(path, 20 << 20);
            if (bytes.empty()) { p = z; continue; }
            std::wstring data = L"data:" + imageMime(path) + L";base64," + widen(base64Encode(bytes));
            html.replace(a, z - a, data);
            p = a + data.size();
        }
    }
    auto swap = [&](const std::wstring& from, const std::wstring& to) {
        for (size_t p = 0; (p = html.find(from, p)) != std::wstring::npos; p += to.size()) html.replace(p, from.size(), to);
    };
    if (!lastDir.empty()) {
        std::wstring dir = lastDir;
        std::replace(dir.begin(), dir.end(), L'\\', L'/');
        swap(L"https://mdlive.doc/", L"file:///" + dir + L"/");
    }
    for (wchar_t d = L'a'; d <= L'z'; d++) swap(std::wstring(L"https://drive-") + d + L".mdlive/", std::wstring(L"file:///") + (wchar_t)towupper(d) + L":/");
    return html;
}

std::vector<std::wstring> splitTabs(const std::wstring& s) {
    std::vector<std::wstring> out;
    size_t start = 0;
    for (size_t tab; (tab = s.find(L'\t', start)) != std::wstring::npos; start = tab + 1) out.push_back(s.substr(start, tab - start));
    out.push_back(s.substr(start));
    return out;
}

void onPanelMessage(const std::wstring& cmd, const std::wstring& arg) {
    if (cmd == L"ready") {
        // Strona mogła zostać przeładowana (np. z menu kontekstowego) - wysyłamy wszystko od nowa.
        sendConfig();
        lastMode = -1;
        refresh(true);
        if (presentPending) startPresentation();
    } else if (cmd == L"open") {
        openLink(arg);
    } else if (cmd == L"goto") {
        gotoLine((int)parseNum(arg));
        SetTimer(panel.hwnd(), T_FOCUS, 60, nullptr);
    } else if (cmd == L"sync") {
        size_t tab = arg.find(L'\t');
        syncEditor(parseNum(arg.substr(0, tab)), tab != std::wstring::npos);
    } else if (cmd == L"theme") {
        cfg.dark = arg == L"dark";
        saveSettings();
        sendConfig();
    } else if (cmd == L"print") {
        // Okno wyboru drukarki albo pliku PDF, potem strona dorysowuje jasne diagramy i odsyła "print-ready".
        // Okno jest modalne, ale komunikaty podglądu dalej płyną - drugie kliknięcie czeka na zamknięcie pierwszego.
        if (printing) return;
        printing = true;
        printPdfPath.clear();
        printPreview = arg != L"pdf";
        printPdfPath = printPreview ? previewPdfPath() : askSavePath(L"pdf", L"PDF\0*.pdf\0");
        if (printPdfPath.empty()) { printing = false; return; }
        panel.post(L"print", L"{\"type\":\"prep-print\"}");
    } else if (cmd == L"print-ready") {
        std::wstring path = printPdfPath;
        panel.printToPdf(path, [path](bool ok) {
            printing = false;
            if (printPreview) {
                // Systemowe okno drukowania Windows z podglądem stron; gdy się nie da - okno drukowania z przeglądarki.
                std::wstring title = L"Markdown Live — " + docBaseName();
                if (!ok) notice(L"pdfError");
                else if (!showSystemPrint(npp._nppHandle, path, title)) panel.showPrintPreview(path, title, npp._nppHandle);
                return;
            }
            if (ok) lastSaved = path;
            notice(ok ? L"pdfSaved" : L"pdfError", fileName(path), ok);
        });
    } else if (cmd == L"print-end") {
        panel.setPrintBackground(false);
    } else if (cmd == L"open-saved") {
        if (!lastSaved.empty()) ShellExecuteW(nullptr, L"open", lastSaved.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (cmd == L"export-html") {
        if (exportPath.empty()) return;
        std::wstring path = exportPath;
        exportPath.clear();
        bool ok = writeFile(path, narrow(externalHtml(arg, true)));
        if (ok) lastSaved = path;
        notice(ok ? L"htmlSaved" : L"saveError", fileName(path), ok);
    } else if (cmd == L"export") {
        cmdExport();
    } else if (cmd == L"copy-rich") {
        // html \x01 zwykły tekst; lokalne obrazki z adresów podglądu na file:///
        size_t sep = arg.find(L'\x01');
        std::wstring html = externalHtml(arg.substr(0, sep), false), text = sep == std::wstring::npos ? L"" : arg.substr(sep + 1);
        notice(setClipboard(npp._nppHandle, text, narrow(html)) ? L"copied" : L"copyError");
    } else if (cmd == L"copy-text") {
        setClipboard(npp._nppHandle, arg);
    } else if (cmd == L"save-image") {
        // nazwa \x01 rodzaj (svg / png) \x01 treść (svg jako tekst, png w base64)
        auto f = std::vector<std::wstring>();
        for (size_t start = 0, sep;; start = sep + 1) {
            sep = arg.find(L'\x01', start);
            f.push_back(arg.substr(start, sep == std::wstring::npos ? std::wstring::npos : sep - start));
            if (sep == std::wstring::npos || f.size() == 3) break;
        }
        if (f.size() < 3) return;
        bool svg = f[1] == L"svg";
        std::wstring path = askSavePath(svg ? L"svg" : L"png", svg ? L"SVG\0*.svg\0" : L"PNG\0*.png\0", f[0]);
        if (path.empty()) return;
        bool ok = writeFile(path, svg ? narrow(f[2]) : base64Decode(narrow(f[2])));
        if (ok) lastSaved = path;
        notice(ok ? L"imageSaved" : L"saveError", fileName(path), ok);
    } else if (cmd == L"task") {
        toggleTask(currentSci(), (Sci_Position)parseNum(arg));
    } else if (cmd == L"toc") {
        if (!lastModUndo) replaceToc(currentSci(), arg);
    } else if (cmd == L"headings") {
        headingSlugs = arg;
    } else if (cmd == L"check") {
        panel.post(L"checked", L"{\"type\":\"checked\"," + checkFiles(arg) + L"}");
    } else if (cmd == L"peek") {
        std::wstring path = localPath(arg);
        std::string text = path.empty() ? std::string() : readFile(path, 64 * 1024);
        panel.post(L"peek", L"{\"type\":\"peek\",\"url\":" + jsonStr(arg) + L",\"text\":" + jsonStr(widen(text)) + L"}");
    } else if (cmd == L"paste-md") {
        if (pasteTarget) insertText(pasteTarget, arg);
        pasteTarget = nullptr;
        SetTimer(panel.hwnd(), T_FOCUS, 10, nullptr);
    } else if (cmd == L"zoom") {
        cfg.zoom = std::clamp((int)parseNum(arg), 50, 300);
        saveSettings();
    } else if (cmd == L"present") {
        startPresentation();
    } else if (cmd == L"present-end") {
        endPresentation();
    } else if (cmd == L"tidy") {
        cmdTidy();
        SetTimer(panel.hwnd(), T_FOCUS, 60, nullptr);
    } else if (cmd == L"toggle-sync") {
        cmdSync();
    } else if (cmd == L"edit") {
        applyEdit(currentSci(), npp._nppHandle, splitTabs(arg), parentDir(currentDoc().path));
        // Podgląd odbiera fokus po puszczeniu przycisku myszy, więc oddajemy go edytorowi chwilę później.
        SetTimer(panel.hwnd(), T_FOCUS, 60, nullptr);
    } else if (cmd == L"force") {
        forced[currentDoc().id] = 1;
        refresh(true);
    }
}

void onPanelTimer(UINT_PTR id) {
    switch (id) {
        case T_RENDER:
            KillTimer(panel.hwnd(), T_RENDER);
            refresh();
            break;
        case T_LAYOUT:
            if (panelVisible() && !(GetKeyState(VK_LBUTTON) & 0x8000)) layout.apply();
            break;
        case T_RESET:
            layout.apply();
            if (++resetTries >= 15) KillTimer(panel.hwnd(), T_RESET);
            break;
        case T_LIST:
            KillTimer(panel.hwnd(), T_LIST);
            continueList(currentSci());
            break;
        case T_FOCUS:
            KillTimer(panel.hwnd(), T_FOCUS);
            SetFocus(currentSci());
            break;
        case T_RENUMBER:
            KillTimer(panel.hwnd(), T_RENUMBER);
            if (listEditing()) renumberFrom(currentSci(), renumberLine, renumberDeleted);
            renumberLine = LLONG_MAX;
            renumberDeleted = false;
            break;
    }
}

void onPanelVisibility(bool visible) {
    setCheck(C_TOGGLE, visible);
    if (!visible) {
        KillTimer(panel.hwnd(), T_RESET);
        return;
    }
    // Panel zawsze otwiera się w układzie 50/50; Notepad++ dokończy rozmieszczenie chwilę później.
    layout.resetToHalf();
    resetTries = 0;
    SetTimer(panel.hwnd(), T_RESET, 40, nullptr);
    lastMode = -1;
    sendConfig();
    refresh(true);
}

// ---------- Ikona paska narzędzi ----------

// Ikona „MD” rysowana piksel po pikselu (16×16), ostra także po powiększeniu o całkowitą krotność.
// '.' tło, 'B' kafelek, 'W' litera.
constexpr const char* kMdIcon[16] = {
    "................",
    "................",
    ".BBBBBBBBBBBBBB.",
    "BBBBBBBBBBBBBBBB",
    "BWWBBBWWBWWWWBBB",
    "BWWWBWWWBWWBWWBB",
    "BWWWWWWWBWWBBWWB",
    "BWWBWBWWBWWBBWWB",
    "BWWBBBWWBWWBBWWB",
    "BWWBBBWWBWWBBWWB",
    "BWWBBBWWBWWBWWBB",
    "BWWBBBWWBWWWWBBB",
    "BBBBBBBBBBBBBBBB",
    ".BBBBBBBBBBBBBB.",
    "................",
    "................",
};
constexpr COLORREF kIconKey = RGB(255, 0, 255);

// Rysuje ikonę do bitmapy wybranej w dc.
void paintMd(HDC dc, int size, COLORREF bg, COLORREF box, COLORREF fg) {
    RECT rc{0, 0, size, size};
    HBRUSH bgBrush = CreateSolidBrush(bg);
    FillRect(dc, &rc, bgBrush);
    DeleteObject(bgBrush);

    int k = std::max(1, size / 16), off = (size - 16 * k) / 2;
    HBRUSH boxBrush = CreateSolidBrush(box), fgBrush = CreateSolidBrush(fg);
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            char c = kMdIcon[y][x];
            if (c == '.') continue;
            RECT px{off + x * k, off + y * k, off + (x + 1) * k, off + (y + 1) * k};
            FillRect(dc, &px, c == 'W' ? fgBrush : boxBrush);
        }
    }
    DeleteObject(boxBrush);
    DeleteObject(fgBrush);
}

HICON makeIcon(int size, COLORREF box, COLORREF fg) {
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, dib);
    paintMd(dc, size, kIconKey, box, fg);
    SelectObject(dc, old);
    GdiFlush();
    auto* px = static_cast<DWORD*>(bits);
    const DWORD key = (DWORD)GetRValue(kIconKey) << 16 | GetGValue(kIconKey) << 8 | GetBValue(kIconKey);
    for (int i = 0; i < size * size; i++) px[i] = (px[i] & 0xffffff) == key ? 0 : (px[i] | 0xff000000);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, dib};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(dib);
    DeleteDC(dc);
    return icon;
}

void addToolbarIcon() {
    UINT dpi = GetDpiForWindow(npp._nppHandle);
    int size = std::max(16, (int)(16 * dpi / 96));
    const COLORREF blue = RGB(0x3d, 0x6d, 0xb5), white = RGB(0xff, 0xff, 0xff);
    toolbarIconsWithDarkMode icons{};
    // Zestaw „standard” Notepad++ używa zwykłej bitmapy zgodnej z ekranem (z DIB pokazuje czarny kwadrat);
    // szare tło (192,192,192) pasek traktuje jako przezroczyste.
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    icons.hToolbarBmp = CreateCompatibleBitmap(screen, 16, 16);
    HGDIOBJ old = SelectObject(dc, icons.hToolbarBmp);
    paintMd(dc, 16, RGB(192, 192, 192), blue, white);
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    icons.hToolbarIcon = makeIcon(size, blue, white);
    icons.hToolbarIconDarkMode = makeIcon(size, RGB(0x8f, 0xb4, 0xea), RGB(0x1b, 0x1b, 0x1b));
    nppMsg(NPPM_ADDTOOLBARICON_FORDARKMODE, funcs[C_TOGGLE]._cmdID, (LPARAM)&icons);
}

// Listy jak w polach czatu (Enter, Tab) działają w Markdown i zwykłym tekście, nie w plikach z kodem.
bool listEditing() {
    int lang = 0;
    nppMsg(NPPM_GETCURRENTLANGTYPE, 0, (LPARAM)&lang);
    if (lang == L_TEXT || lang == L_USER) return true;
    wchar_t path[MAX_PATH * 2] = {};
    nppMsg(NPPM_GETFULLCURRENTPATH, MAX_PATH * 2, (LPARAM)path);
    return hasMarkdownExtension(path);
}

constexpr UINT_PTR kSciSubclass = 0x4D45;
bool eatTabChar = false;

// Tab / Shift+Tab w punkcie listy: podpunkt. Poza listą Tab trafia do Notepad++ bez zmian.
bool eatEnterChar = false, plainPaste = false;
bool dropOnEditor(HWND target, HDROP drop);
constexpr UINT_PTR kDropSubclass = 0x4D46;
bool keyDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

// Sprytne wklejanie tylko przy otwartym podglądzie z Markdownem; w pozostałych plikach Ctrl+V działa zwyczajnie.
bool previewShowsMarkdown() { return panelVisible() && lastMode == 1; }

// Wklejanie w Markdown: obrazek ze schowka -> plik PNG, pliki z Eksploratora -> linki,
// sformatowany tekst ze strony WWW -> Markdown (zamiana w podglądzie). Ctrl+Shift+V wkleja zwykły tekst.
bool smartPaste(HWND h) {
    if (plainPaste) { plainPaste = false; return false; }
    if (pasteLinkOverSelection(h)) return true;
    auto files = clipboardFiles(h);
    if (!files.empty()) { insertFileLinks(h, files); return true; }
    if (clipboardHasImageOnly()) return pasteImage(h);
    if (!panel.ready() || pasteTarget) return false;
    std::string html = clipboardHtml(h);
    if (html.empty()) return false;
    pasteTarget = h;
    panel.post(L"html2md", L"{\"type\":\"html2md\",\"html\":" + jsonStr(widen(html)) + L"}");
    return true;
}

// Tab / Shift+Tab w punkcie listy: podpunkt. Tab / Enter w tabeli: komórki i wiersze. Poza tym bez zmian.
LRESULT CALLBACK sciProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_TAB && !keyDown(VK_CONTROL) && !keyDown(VK_MENU) && !SendMessageW(h, SCI_AUTOCACTIVE, 0, 0) && listEditing() &&
        (tableKey(h, false, keyDown(VK_SHIFT)) || indentListItem(h, keyDown(VK_SHIFT)))) {
        eatTabChar = true;
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN && !keyDown(VK_CONTROL) && !keyDown(VK_MENU) && !keyDown(VK_SHIFT) && listEditing() &&
        tableKey(h, true, false)) {
        eatEnterChar = true;
        return 0;
    }
    if (msg == WM_CHAR && ((wp == L'\t' && eatTabChar) || (wp == L'\r' && eatEnterChar))) {
        eatTabChar = eatEnterChar = false;
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == 'V' && keyDown(VK_CONTROL) && keyDown(VK_SHIFT) && !keyDown(VK_MENU) && listEditing()) {
        plainPaste = true;
        SendMessageW(h, SCI_PASTE, 0, 0);
        plainPaste = false;
        return 0;
    }
    // Zaznaczenie + * _ ` ~ = otoczenie zaznaczenia; zaznaczenie + wklejony adres = link.
    if (msg == WM_CHAR && (wp == L'*' || wp == L'_' || wp == L'`' || wp == L'~') && listEditing() && wrapSelectionWith(h, (wchar_t)wp)) return 0;
    if ((msg == SCI_PASTE || msg == WM_PASTE) && previewShowsMarkdown() && smartPaste(h)) return 0;
    if (msg == WM_DROPFILES && dropOnEditor(h, (HDROP)wp)) return 0;
    return DefSubclassProc(h, msg, wp, lp);
}

// Pliki upuszczone na edytor Markdown: obrazki (albo dowolne pliki z wciśniętym Shiftem) jako linki w miejscu upuszczenia.
// Bez Shiftu inne pliki Notepad++ otwiera jak zwykle.
bool dropOnEditor(HWND target, HDROP drop) {
    POINT pt;
    GetCursorPos(&pt);
    HWND under = WindowFromPoint(pt);
    HWND h = target == npp._scintillaMainHandle || target == npp._scintillaSecondHandle ? target : under;
    if (h != currentSci() || !previewShowsMarkdown()) return false;
    auto files = dropFiles(drop);
    bool images = !files.empty() && std::all_of(files.begin(), files.end(), [](const std::wstring& f) { return isImageFile(f); });
    if (files.empty() || (!images && !keyDown(VK_SHIFT))) return false;
    ScreenToClient(h, &pt);
    Sci_Position pos = sci(h, SCI_POSITIONFROMPOINT, pt.x, pt.y);
    sci(h, SCI_SETSEL, pos, pos);
    insertFileLinks(h, files);
    SetForegroundWindow(npp._nppHandle);
    SetFocus(h);
    return true;
}

LRESULT CALLBACK dropProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_DROPFILES && dropOnEditor(h, (HDROP)wp)) return 0;
    return DefSubclassProc(h, msg, wp, lp);
}

// Ctrl+V i „Wklej” z menu: Notepad++ wkleja bezpośrednio przez Scintillę (z pominięciem okna edytora),
// więc polecenie przechwytujemy w głównym oknie.
constexpr UINT_PTR kPasteSubclass = 0x4D47;
constexpr WORD kIdmEditPaste = 42005;   // IDM_EDIT_PASTE

LRESULT CALLBACK pasteProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_COMMAND && LOWORD(wp) == kIdmEditPaste) {
        HWND ed = currentSci();
        if (GetFocus() == ed && previewShowsMarkdown() && smartPaste(ed)) return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void registerPanel() {
    std::wstring dataDir = pluginsConfigDir() + L"\\MarkdownLive";
    SHCreateDirectoryExW(nullptr, (dataDir + L"\\webview2").c_str(), nullptr);

    panel.onMessage = onPanelMessage;
    panel.onVisibility = onPanelVisibility;
    panel.onClose = [] { autoShown = false; };
    panel.onTimer = onPanelTimer;
    if (!panel.create(gInst, npp._nppHandle, pluginDir, dataDir + L"\\webview2")) return;

    static DockedWidgetData dock;
    dock.hClient = panel.hwnd();
    dock.pszName = L"Markdown";
    dock.dlgID = funcs[C_TOGGLE]._cmdID;
    dock.uMask = DWS_DF_CONT_RIGHT | DWS_ICONTAB;
    dock.hIconTab = makeIcon(16, RGB(0x3d, 0x6d, 0xb5), RGB(0xff, 0xff, 0xff));
    dock.pszModuleName = kModule;
    nppMsg(NPPM_DMMREGASDCKDLG, 0, (LPARAM)&dock);

    layout.attach(npp._nppHandle, npp._scintillaMainHandle, npp._scintillaSecondHandle, panel.hwnd());
    SetWindowSubclass(npp._scintillaMainHandle, sciProc, kSciSubclass, 0);
    SetWindowSubclass(npp._scintillaSecondHandle, sciProc, kSciSubclass, 0);
    // Upuszczone pliki Scintilla przekazuje oknu-rodzicowi jako WM_DROPFILES.
    for (HWND p : {GetParent(npp._scintillaMainHandle), GetParent(npp._scintillaSecondHandle)}) {
        if (p) SetWindowSubclass(p, dropProc, kDropSubclass, 0);
    }
    SetWindowSubclass(npp._nppHandle, pasteProc, kPasteSubclass, 0);
    SetTimer(panel.hwnd(), T_LAYOUT, 250, nullptr);
}

void setItem(Cmd c, PFUNCPLUGINCMD fn, bool check = false, ShortcutKey* key = nullptr) {
    funcs[c]._pFunc = fn;
    funcs[c]._init2Check = check;
    funcs[c]._pShKey = key;
}

const wchar_t* itemName(Cmd c) {
    switch (c) {
        case C_TOGGLE: return tr(L"menuToggle", L"Show / hide preview");
        case C_REFRESH: return tr(L"menuRefresh", L"Refresh preview");
        case C_FORCE: return tr(L"menuForce", L"Treat this document as Markdown");
        case C_SYNC: return tr(L"menuSync", L"Synchronize scrolling");
        case C_AUTO: return tr(L"menuAuto", L"Open preview automatically for Markdown");
        case C_RESET: return tr(L"menuReset", L"Reset to 50/50 layout");
        case C_COPY: return tr(L"menuCopy", L"Copy as formatted text");
        case C_EXPORT: return tr(L"menuExport", L"Export to HTML...");
        case C_TIDY: return tr(L"menuTidy", L"Tidy up document");
        case C_PRESENT: return tr(L"menuPresent", L"Presentation");
        case C_ABOUT: return tr(L"menuAbout", L"About...");
        default: return L"-";
    }
}

void nameItems() {
    for (int c = 0; c < C_COUNT; c++) {
        wcsncpy(funcs[c]._itemName, itemName((Cmd)c), menuItemSize - 1);
        funcs[c]._itemName[menuItemSize - 1] = 0;
    }
}

void loadLanguage() {
    std::wstring nppDir = parentDir(parentDir(pluginsConfigDir()));
    i18n.load(pluginDir + L"\\assets\\i18n", detectNppLanguage(npp._nppHandle, iniPath, nppDir));
}

// Zmiana języka w Notepad++ w trakcie pracy: nowe nazwy w menu i w podglądzie.
void onLanguageChanged() {
    std::wstring before = i18n.lang();
    loadLanguage();
    if (i18n.lang() == before) return;
    nameItems();
    HMENU menu = (HMENU)nppMsg(NPPM_GETMENUHANDLE, NPPPLUGINMENU, 0);
    for (Cmd c : {C_TOGGLE, C_REFRESH, C_FORCE, C_SYNC, C_AUTO, C_RESET, C_COPY, C_EXPORT, C_TIDY, C_PRESENT, C_ABOUT}) {
        std::wstring label = itemName(c);
        if (c == C_TOGGLE) label += L"\tCtrl+Alt+M";
        ModifyMenuW(menu, funcs[c]._cmdID, MF_BYCOMMAND | MF_STRING, funcs[c]._cmdID, label.c_str());
    }
    setCheck(C_TOGGLE, panelVisible());
    setCheck(C_SYNC, cfg.sync);
    setCheck(C_AUTO, cfg.autoOpen);
    refresh(true);
    sendConfig();
}

} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        gInst = inst;
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(inst, path, MAX_PATH);
        pluginDir = parentDir(path);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData data) {
    npp = data;
    iniPath = pluginsConfigDir() + L"\\MarkdownLive.ini";
    loadLanguage();
    cfg.dark = GetPrivateProfileIntW(kIniSection, L"Dark", 0, iniPath.c_str()) != 0;
    cfg.sync = GetPrivateProfileIntW(kIniSection, L"SyncScroll", 1, iniPath.c_str()) != 0;
    cfg.autoOpen = GetPrivateProfileIntW(kIniSection, L"AutoOpen", 1, iniPath.c_str()) != 0;
    cfg.zoom = std::clamp((int)GetPrivateProfileIntW(kIniSection, L"Zoom", 100, iniPath.c_str()), 50, 300);

    setItem(C_TOGGLE, cmdToggle, false, &toggleKey);
    setItem(C_REFRESH, cmdRefresh);
    setItem(C_FORCE, cmdForce);
    setItem(C_SEP, nullptr);
    setItem(C_SYNC, cmdSync, cfg.sync);
    setItem(C_AUTO, cmdAuto, cfg.autoOpen);
    setItem(C_RESET, cmdReset);
    setItem(C_SEP2, nullptr);
    setItem(C_COPY, cmdCopy);
    setItem(C_EXPORT, cmdExport);
    setItem(C_TIDY, cmdTidy);
    setItem(C_PRESENT, cmdPresent);
    setItem(C_SEP3, nullptr);
    setItem(C_ABOUT, cmdAbout);
    nameItems();
}

extern "C" __declspec(dllexport) const wchar_t* getName() { return L"Markdown Live"; }

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) {
    *count = C_COUNT;
    return funcs;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* n) {
    HWND from = (HWND)n->nmhdr.hwndFrom;

    if (from == npp._nppHandle) {
        switch (n->nmhdr.code) {
            case NPPN_TBMODIFICATION:
                addToolbarIcon();
                registerPanel();
                break;
            case NPPN_READY:
                onLanguageChanged();   // teraz Notepad++ na pewno zna swój język
                if (panelVisible()) onPanelVisibility(true); else refresh(false, true);
                break;
            case NPPN_NATIVELANGCHANGED:
                onLanguageChanged();
                break;
            case NPPN_BUFFERACTIVATED:
            case NPPN_LANGCHANGED:
            case NPPN_FILESAVED:
            case NPPN_FILERENAMED:
                if (panel.hwnd()) refresh(true, n->nmhdr.code == NPPN_BUFFERACTIVATED);
                break;
            case NPPN_FILECLOSED:
            case NPPN_FILEBEFORECLOSE:
                break;
            case NPPN_SHUTDOWN:
                layout.detach();
                RemoveWindowSubclass(npp._scintillaMainHandle, sciProc, kSciSubclass);
                RemoveWindowSubclass(npp._scintillaSecondHandle, sciProc, kSciSubclass);
                for (HWND p : {GetParent(npp._scintillaMainHandle), GetParent(npp._scintillaSecondHandle)}) {
                    if (p) RemoveWindowSubclass(p, dropProc, kDropSubclass);
                }
                RemoveWindowSubclass(npp._nppHandle, pasteProc, kPasteSubclass);
                endPresentation();
                break;
        }
        return;
    }

    if ((from != npp._scintillaMainHandle && from != npp._scintillaSecondHandle) || from != currentSci() || !panel.hwnd()) return;
    switch (n->nmhdr.code) {
        case SCN_CHARADDED:
            // Enter w liście: kontynuacja po tym, jak Notepad++ skończy własne wcięcie.
            // Działa w Markdown i zwykłym tekście (także w nowym, krótkim dokumencie), nie w plikach z kodem.
            if ((n->ch == '\n' || n->ch == '\r') && listEditing()) SetTimer(panel.hwnd(), T_LIST, 1, nullptr);
            else if (isMarkdown(currentDoc())) onCharForCompletion(from, n->ch);
            break;
        case SCN_AUTOCCOMPLETED:
            onCompletionDone(from);
            break;
        case SCN_AUTOCCANCELLED:
            acKind = AC_NONE;
            break;
        case SCN_MODIFIED:
            if (n->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
                lastModUndo = (n->modificationType & (SC_PERFORMED_UNDO | SC_PERFORMED_REDO)) != 0;
                scheduleRefresh();
                // Usunięte / dodane całe linie (np. skasowany punkt listy): numery poniżej do wyrównania.
                // Cofanie i ponawianie pomijamy, żeby Ctrl+Z zawsze przywracał stan.
                if (n->linesAdded != 0 && !(n->modificationType & (SC_PERFORMED_UNDO | SC_PERFORMED_REDO))) {
                    renumberLine = std::min<long long>(renumberLine, sci(from, SCI_LINEFROMPOSITION, n->position));
                    renumberDeleted = n->linesAdded < 0;   // nowsze wstawienie unieważnia wcześniejsze usunięcie
                    SetTimer(panel.hwnd(), T_RENUMBER, 1, nullptr);
                }
            }
            break;
        case SCN_UPDATEUI:
            if (n->updated & SC_UPDATE_V_SCROLL) sendScroll();
            if (n->updated & (SC_UPDATE_SELECTION | SC_UPDATE_CONTENT)) sendCaretBlock();
            break;
        case SCN_DOUBLECLICK:
            if (panelVisible() && lastMode == 1) {
                // n->line, nie n->position: dwuklik za końcem tekstu linii daje pozycję -1 (= linia 0, skok na początek).
                long long line = n->line >= 0 ? (long long)n->line : (long long)sci(from, SCI_LINEFROMPOSITION, n->position);
                panel.post(L"reveal", L"{\"type\":\"reveal\",\"line\":" + std::to_wstring(line) + L"}");
            }
            break;
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }

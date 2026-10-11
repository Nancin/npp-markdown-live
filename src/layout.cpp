#include "layout.h"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <initializer_list>
#include <string>

namespace {

constexpr UINT kMoveSplitter = 0x5000 + 11;   // DMM_MOVE_SPLITTER z Docking Managera
constexpr UINT_PTR kSubclassId = 0x4D44;
constexpr int kMinTotal = 300;

std::wstring className(HWND h) {
    wchar_t buf[64] = {};
    GetClassNameW(h, buf, 64);
    return buf;
}

struct Find { const wchar_t* cls; HWND result; const RECT* edge; bool right; };

BOOL CALLBACK findManager(HWND h, LPARAM lp) {
    auto* f = reinterpret_cast<Find*>(lp);
    if (className(h) == f->cls) { f->result = h; return FALSE; }
    return TRUE;
}

// Pionowy pasek dokowania przylegający do panelu z lewej (panel po prawej) lub z prawej.
BOOL CALLBACK findSplitter(HWND h, LPARAM lp) {
    auto* f = reinterpret_cast<Find*>(lp);
    if (className(h) != f->cls || !IsWindowVisible(h)) return TRUE;
    RECT r;
    GetWindowRect(h, &r);
    if (std::abs(r.right - f->edge->left) <= 6) { f->result = h; f->right = true; return FALSE; }
    if (std::abs(r.left - f->edge->right) <= 6) { f->result = h; f->right = false; return FALSE; }
    return TRUE;
}

} // namespace

void DockLayout::attach(HWND npp, HWND sciMain, HWND sciSecond, HWND panel) {
    npp_ = npp; sciMain_ = sciMain; sciSecond_ = sciSecond; panel_ = panel;
    Find f{L"dockingManager", nullptr, nullptr, true};
    EnumChildWindows(npp_, findManager, reinterpret_cast<LPARAM>(&f));
    manager_ = f.result;
    SetWindowSubclass(npp_, nppProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this));
    if (manager_) SetWindowSubclass(manager_, managerProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this));
}

void DockLayout::detach() {
    if (npp_) RemoveWindowSubclass(npp_, nppProc, kSubclassId);
    if (manager_) RemoveWindowSubclass(manager_, managerProc, kSubclassId);
    npp_ = manager_ = nullptr;
}

DockLayout::Metrics DockLayout::measure() const {
    Metrics m;
    if (!npp_ || !panel_ || !IsWindowVisible(panel_)) return m;

    // Kontener panelu to potomek okna głównego; pływający panel ma innego rodzica.
    HWND box = panel_;
    while (box && GetParent(box) != npp_) box = GetParent(box);
    if (!box || !IsWindowVisible(box)) return m;

    RECT pr, edit{};
    GetWindowRect(box, &pr);
    for (HWND v : {sciMain_, sciSecond_}) {
        RECT r;
        if (!v || !IsWindowVisible(v) || !GetWindowRect(v, &r)) continue;
        if (IsRectEmpty(&edit)) edit = r; else UnionRect(&edit, &edit, &r);
    }
    if (IsRectEmpty(&edit)) return m;

    Find f{L"wedockspliter", nullptr, &pr, true};
    EnumChildWindows(npp_, findSplitter, reinterpret_cast<LPARAM>(&f));
    if (!f.result) return m;

    m.ok = true;
    m.rightSide = f.right;
    m.splitter = f.result;
    m.panelW = pr.right - pr.left;
    m.editorW = edit.right - edit.left;
    return m;
}

void DockLayout::apply() {
    if (!manager_) return;
    for (int pass = 0; pass < 3; pass++) {
        Metrics m = measure();
        int total = m.panelW + m.editorW;
        if (!m.ok || total < kMinTotal) return;
        int delta = (int)std::lround(ratio_ * total) - m.panelW;
        if (std::abs(delta) <= 1) return;
        adjusting_ = true;
        SendMessageW(manager_, kMoveSplitter, (WPARAM)(m.rightSide ? delta : -delta), (LPARAM)m.splitter);
        adjusting_ = false;
    }
}

// Użytkownik przesunął pasek: nowe proporcje stają się obowiązujące.
void DockLayout::adoptRatio() {
    Metrics m = measure();
    int total = m.panelW + m.editorW;
    if (!m.ok || total < kMinTotal) return;
    ratio_ = std::abs(2 * m.panelW - total) <= 2 ? 0.5 : (double)m.panelW / total;
}

// Przeciąganie paska z przyciąganiem do 50/50. Szerokość „bez przyciągania” liczymy osobno,
// więc po wyjściu ze strefy pasek wraca dokładnie pod kursor.
LRESULT DockLayout::userDrag(HWND h, WPARAM wp, LPARAM lp, const Metrics& m) {
    int total = m.panelW + m.editorW;
    ULONGLONG now = GetTickCount64();
    if (now - lastDrag_ > 250) dragRaw_ = m.panelW;
    lastDrag_ = now;

    int offset = (int)(LONG_PTR)wp;
    dragRaw_ += m.rightSide ? offset : -offset;
    int half = total / 2;
    int zone = std::max(16, total / 40);
    int target = std::abs(dragRaw_ - half) <= zone ? half : dragRaw_;
    int grow = target - m.panelW;
    if (grow == 0) return 0;

    LRESULT r = DefSubclassProc(h, kMoveSplitter, (WPARAM)(LONG_PTR)(m.rightSide ? grow : -grow), lp);
    adoptRatio();
    return r;
}

LRESULT CALLBACK DockLayout::nppProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    LRESULT r = DefSubclassProc(h, msg, wp, lp);
    if (msg == WM_SIZE && wp != SIZE_MINIMIZED) reinterpret_cast<DockLayout*>(data)->apply();
    return r;
}

LRESULT CALLBACK DockLayout::managerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* self = reinterpret_cast<DockLayout*>(data);
    if (msg == kMoveSplitter && !self->adjusting_) {
        Metrics m = self->measure();
        if (m.ok && (HWND)lp == m.splitter && m.panelW + m.editorW >= kMinTotal) return self->userDrag(h, wp, lp, m);
    }
    return DefSubclassProc(h, msg, wp, lp);
}

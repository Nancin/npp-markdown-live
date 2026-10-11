#pragma once
#include <windows.h>

// Utrzymuje proporcje edytor : podgląd (domyślnie 50/50) niezależnie od rozmiaru okna.
// Szerokością dokowanego panelu steruje się komunikatem DMM_MOVE_SPLITTER menedżera dokowania.
class DockLayout {
public:
    void attach(HWND npp, HWND sciMain, HWND sciSecond, HWND panel);
    void detach();

    void resetToHalf() { ratio_ = 0.5; }
    void apply();

private:
    struct Metrics {
        bool ok = false;
        bool rightSide = true;
        int panelW = 0, editorW = 0;
        HWND splitter = nullptr;
    };

    Metrics measure() const;
    void adoptRatio();
    LRESULT userDrag(HWND h, WPARAM wp, LPARAM lp, const Metrics& m);

    static LRESULT CALLBACK nppProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK managerProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    HWND npp_ = nullptr, sciMain_ = nullptr, sciSecond_ = nullptr, panel_ = nullptr, manager_ = nullptr;
    double ratio_ = 0.5;
    bool adjusting_ = false;
    int dragRaw_ = 0;              // szerokość, jaką panel miałby bez przyciągania do środka
    ULONGLONG lastDrag_ = 0;
};

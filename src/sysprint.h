#pragma once
#include <windows.h>
#include <string>

// Systemowe okno drukowania Windows z podglądem stron (Windows 10/11).
// Strony pochodzą z gotowego PDF podglądu i są rysowane przez Direct2D: w podglądzie okna i na drukarce.
// false = nie udało się przygotować (brak PDF, starszy system) - wtedy można użyć innego sposobu druku.
bool showSystemPrint(HWND owner, const std::wstring& pdfPath, const std::wstring& title);

#pragma once
#include <windows.h>
#include "Sci_Position.h"
#include <string>
#include <vector>

// Polecenie z paska edycji podglądu: pola rozdzielone tabulatorami (rodzaj, argumenty...).
// docDir: folder zapisanego dokumentu (pusty dla niezapisanego) - do ścieżek obrazków.
void applyEdit(HWND sci, HWND owner, const std::vector<std::wstring>& fields, const std::wstring& docDir);

// Enter w punkcie listy: nowy punkt z kolejnym numerem; Enter w pustym punkcie kończy listę.
// Wołać po przetworzeniu Entera przez Notepad++ (po jego automatycznym wcięciu).
void continueList(HWND sci);

// Tab / Shift+Tab w punkcie listy: podpunkt / powrót poziom wyżej. false = to nie lista, Tab działa zwykle.
bool indentListItem(HWND sci, bool outdent);

// Po usunięciu / wstawieniu całych linii: wyrównanie numerów listy od podanej linii w dół.
// afterDelete: linie zostały usunięte, więc punkt w `line` mógł zostać nowym początkiem listy (od 1).
void renumberFrom(HWND sci, Sci_Position line, bool afterDelete);

// Zaznaczenie + wpisany znak * _ ` ~ : otoczenie zaznaczenia zamiast jego zastąpienia.
bool wrapSelectionWith(HWND sci, wchar_t ch);

// Zaznaczenie + wklejony adres: link [zaznaczenie](adres).
bool pasteLinkOverSelection(HWND sci);

// Tab / Shift+Tab / Enter w wierszu tabeli: następna / poprzednia komórka, nowy wiersz; tabela zostaje wyrównana.
// false = kursor nie stoi w tabeli, klawisz działa zwykle.
bool tableKey(HWND sci, bool enter, bool back);

// Porządki: listy na "-", wyrównane tabele, odstępy wokół nagłówków, bez spacji na końcach linii.
void tidyDocument(HWND sci);

// Spis treści między <!-- toc --> a <!-- tocstop -->: podmienia treść, jeśli się zmieniła.
void replaceToc(HWND sci, const std::wstring& body);

// Pole zadania w podanej linii: [ ] <-> [x].
void toggleTask(HWND sci, Sci_Position line);

// Wstawia tekst (UTF-16) w miejsce zaznaczenia jako jedną operację cofania.
void insertText(HWND sci, const std::wstring& text);

// Ścieżka pliku zapisana do Markdown: względna dla zapisanego dokumentu, z ukośnikami i %20.
std::wstring markdownPath(const std::wstring& file, const std::wstring& docDir);

#include "edit.h"
#include <commdlg.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include "Scintilla.h"

namespace {

constexpr char kSel = '\x01';   // znacznik zaznaczenia w szablonach z podglądu

struct Sci {
    HWND h;
    LRESULT operator()(UINT m, WPARAM w = 0, LPARAM l = 0) const { return SendMessageW(h, m, w, l); }
};

std::string encode(const std::wstring& w, UINT cp) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string eol(const Sci& s) {
    switch (s(SCI_GETEOLMODE)) {
        case SC_EOL_CR: return "\r";
        case SC_EOL_LF: return "\n";
        default: return "\r\n";
    }
}

std::string withEol(const std::string& text, const std::string& e) {
    std::string out;
    for (char c : text) {
        if (c == '\n') out += e; else out += c;
    }
    return out;
}

std::string range(const Sci& s, Sci_Position a, Sci_Position b) {
    if (b <= a) return {};
    return std::string((const char*)s(SCI_GETRANGEPOINTER, a, b - a), (size_t)(b - a));
}

void replaceRange(const Sci& s, Sci_Position a, Sci_Position b, const std::string& text) {
    s(SCI_SETTARGETRANGE, a, b);
    s(SCI_REPLACETARGET, text.size(), (LPARAM)text.data());
}

// Wstawia tekst w miejsce [a, b); znaczniki \x01 wyznaczają zaznaczenie po wstawieniu.
void put(const Sci& s, Sci_Position a, Sci_Position b, std::string text) {
    size_t m1 = text.find(kSel), m2 = std::string::npos;
    if (m1 != std::string::npos) {
        text.erase(m1, 1);
        m2 = text.find(kSel, m1);
        if (m2 != std::string::npos) text.erase(m2, 1); else m2 = m1;
    }
    replaceRange(s, a, b, text);
    if (m1 != std::string::npos) s(SCI_SETSEL, a + m1, a + m2);
    else s(SCI_GOTOPOS, a + text.size());
}

// Spacja przed wstawką, gdy kursor stoi tuż za słowem.
std::string gap(const Sci& s, Sci_Position a) {
    if (a == 0) return "";
    char c = (char)s(SCI_GETCHARAT, a - 1);
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' || c == '[') ? "" : " ";
}

// Otacza zaznaczenie znacznikami; ponowne użycie zdejmuje je.
void wrap(const Sci& s, const std::string& before, const std::string& after, const std::string& placeholder) {
    Sci_Position a = s(SCI_GETSELECTIONSTART), b = s(SCI_GETSELECTIONEND), len = s(SCI_GETLENGTH);
    Sci_Position nb = (Sci_Position)before.size(), na = (Sci_Position)after.size();
    std::string sel = range(s, a, b);

    if (a >= nb && b + na <= len && range(s, a - nb, a) == before && range(s, b, b + na) == after) {
        replaceRange(s, b, b + na, "");
        replaceRange(s, a - nb, a, "");
        s(SCI_SETSEL, a - nb, b - nb);
        return;
    }
    if ((Sci_Position)sel.size() >= nb + na && sel.compare(0, nb, before) == 0 && sel.compare(sel.size() - na, na, after) == 0) {
        put(s, a, b, kSel + sel.substr(nb, sel.size() - nb - na) + kSel);
        return;
    }
    if (sel.empty()) put(s, a, b, gap(s, a) + before + kSel + placeholder + kSel + after);
    else put(s, a, b, before + kSel + sel + kSel + after);
}

void inlineTemplate(const Sci& s, const std::string& whenEmpty, std::string withSel) {
    Sci_Position a = s(SCI_GETSELECTIONSTART), b = s(SCI_GETSELECTIONEND);
    std::string sel = range(s, a, b);
    if (sel.empty()) { put(s, a, b, gap(s, a) + whenEmpty); return; }
    size_t at = withSel.find("{sel}");
    if (at != std::string::npos) withSel.replace(at, 5, sel);
    put(s, a, b, withSel);
}

// ----- Znaczniki na początku linii (nagłówki, listy, cytat) -----

enum class Kind { Heading, List, Quote };

size_t headingLen(const std::string& t) {
    size_t n = 0;
    while (n < t.size() && t[n] == '#') n++;
    if (n == 0 || n > 6) return 0;
    if (n == t.size()) return n;
    return t[n] == ' ' ? n + 1 : 0;
}

// Długość dowolnego znacznika listy ("- ", "* ", "1. ", "- [ ] ").
size_t listLen(const std::string& t, std::string* type) {
    if (t.size() >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') {
        if (t.size() >= 6 && t[2] == '[' && (t[3] == ' ' || t[3] == 'x' || t[3] == 'X') && t[4] == ']' && t[5] == ' ') {
            *type = "task";
            return 6;
        }
        *type = "ul";
        return 2;
    }
    size_t d = 0;
    while (d < t.size() && d < 9 && isdigit((unsigned char)t[d])) d++;
    if (d > 0 && d < t.size() && (t[d] == '.' || t[d] == ')') && (d + 1 == t.size() || t[d + 1] == ' ')) {
        *type = "ol";
        return std::min(t.size(), d + 2);
    }
    return 0;
}

size_t quoteLen(const std::string& t) {
    if (t.empty() || t[0] != '>') return 0;
    return t.size() > 1 && t[1] == ' ' ? 2 : 1;
}

int numberAfter(const Sci& s, Sci_Position line, int indent);   // niżej, przy Item
void renumberList(const Sci& s, Sci_Position from, Sci_Position startAt);

void lines(const Sci& s, const std::string& mode) {
    Sci_Position a = s(SCI_GETSELECTIONSTART), b = s(SCI_GETSELECTIONEND);
    Sci_Position first = s(SCI_LINEFROMPOSITION, a), lastLine = s(SCI_LINEFROMPOSITION, b);
    if (lastLine > first && b == s(SCI_POSITIONFROMLINE, lastLine)) lastLine--;

    // "h0" = zwykły akapit: zdejmuje nagłówek, listę i cytat.
    if (mode == "h0") {
        for (Sci_Position ln = lastLine; ln >= first; ln--) {
            Sci_Position st = s(SCI_POSITIONFROMLINE, ln), en = s(SCI_GETLINEENDPOSITION, ln);
            std::string text = range(s, st, en);
            size_t indent = text.find_first_not_of(" \t");
            if (indent == std::string::npos) continue;
            std::string body = text.substr(indent), type;
            size_t cut = headingLen(body);
            if (!cut) cut = quoteLen(body);
            if (!cut) cut = listLen(body, &type);
            if (cut) replaceRange(s, st + (Sci_Position)indent, st + (Sci_Position)(indent + cut), "");
        }
        s(SCI_GOTOPOS, s(SCI_GETLINEENDPOSITION, lastLine));
        return;
    }

    Kind kind = mode[0] == 'h' ? Kind::Heading : mode == "quote" ? Kind::Quote : Kind::List;
    int level = kind == Kind::Heading ? mode[1] - '0' : 0;

    struct Line { Sci_Position start; size_t indent, marker; std::string type; bool blank; };
    std::vector<Line> rows;
    bool allHave = true;
    for (Sci_Position ln = first; ln <= lastLine; ln++) {
        Sci_Position st = s(SCI_POSITIONFROMLINE, ln), en = s(SCI_GETLINEENDPOSITION, ln);
        std::string text = range(s, st, en);
        Line row{st, text.find_first_not_of(" \t"), 0, "", false};
        if (row.indent == std::string::npos) { row.indent = text.size(); row.blank = true; }
        std::string body = text.substr(row.indent);
        if (kind == Kind::Heading) {
            row.marker = headingLen(body);
            bool same = row.marker && body.find_first_not_of('#') == (size_t)level;
            if (!row.blank && !same) allHave = false;
        } else if (kind == Kind::Quote) {
            row.marker = quoteLen(body);
            if (!row.blank && !row.marker) allHave = false;
        } else {
            row.marker = listLen(body, &row.type);
            if (!row.blank && row.type != mode) allHave = false;
        }
        rows.push_back(row);
    }
    bool single = rows.size() == 1;
    // Same puste linie (np. nowa linia pod kursorem): znacznik zawsze dodajemy.
    if (std::all_of(rows.begin(), rows.end(), [](const Line& r) { return r.blank; })) allHave = false;

    // Numeracja kontynuuje listę z tego samego poziomu powyżej (np. po wyjściu z podpunktu: 3., nie 1.).
    int first0 = mode == "ol" && !allHave ? std::max(1, numberAfter(s, first, (int)rows.front().indent)) - 1 : 0;

    // Od końca, żeby wcześniejsze pozycje się nie przesuwały.
    int number = first0;
    for (const auto& r : rows) if (!r.blank || single) number++;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        if (it->blank && !single) continue;
        std::string marker;
        if (!allHave) {
            if (kind == Kind::Heading) marker = std::string(level, '#') + " ";
            else if (kind == Kind::Quote) marker = "> ";
            else if (mode == "ul") marker = "- ";
            else if (mode == "task") marker = "- [ ] ";
            else marker = std::to_string(number) + ". ";
        }
        number--;
        Sci_Position p = it->start + (Sci_Position)it->indent;
        replaceRange(s, p, p + (Sci_Position)it->marker, marker);
    }
    if (mode == "ol" || kind == Kind::List) renumberList(s, lastLine + 1, lastLine + 1);
    Sci_Position st = s(SCI_POSITIONFROMLINE, first), en = s(SCI_GETLINEENDPOSITION, lastLine);
    if (single) s(SCI_GOTOPOS, en); else s(SCI_SETSEL, st, en);
}

// Blok (tabela, kod, wzór...) zawsze w osobnych liniach, oddzielony pustą linią.
void block(const Sci& s, const std::string& tpl) {
    std::string e = eol(s);
    std::string text = withEol(tpl, e);
    Sci_Position b = s(SCI_GETSELECTIONEND);
    Sci_Position ln = s(SCI_LINEFROMPOSITION, b);
    Sci_Position st = s(SCI_POSITIONFROMLINE, ln), en = s(SCI_GETLINEENDPOSITION, ln);
    bool blank = range(s, st, en).find_first_not_of(" \t") == std::string::npos;

    Sci_Position lines = s(SCI_GETLINECOUNT);
    bool nextFilled = false;
    if (ln + 1 < lines) {
        Sci_Position ns = s(SCI_POSITIONFROMLINE, ln + 1), ne = s(SCI_GETLINEENDPOSITION, ln + 1);
        nextFilled = range(s, ns, ne).find_first_not_of(" \t") != std::string::npos;
    }
    bool prevFilled = false;
    if (blank && ln > 0) {
        Sci_Position ps = s(SCI_POSITIONFROMLINE, ln - 1), pe = s(SCI_GETLINEENDPOSITION, ln - 1);
        prevFilled = range(s, ps, pe).find_first_not_of(" \t") != std::string::npos;
    }

    std::string out = (blank ? (prevFilled ? e : "") : e + e) + text + (nextFilled ? e : "");
    if (text.find(kSel) == std::string::npos) out += kSel;
    if (blank) put(s, st, en, out); else put(s, en, en, out);
}

std::wstring pickImage(HWND owner) {
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.gif;*.webp;*.svg;*.bmp;*.ico\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? file : L"";
}

// Ścieżka do Markdown: względna dla zapisanego dokumentu, inaczej pełna.
std::wstring imagePath(const std::wstring& file, const std::wstring& docDir) {
    std::wstring out = file;
    wchar_t rel[MAX_PATH * 2];
    if (!docDir.empty() && PathRelativePathToW(rel, docDir.c_str(), FILE_ATTRIBUTE_DIRECTORY, file.c_str(), 0)) {
        out = rel;
        if (out.rfind(L".\\", 0) == 0) out = out.substr(2);
    }
    std::wstring url;
    for (wchar_t c : out) {
        if (c == L'\\') url += L'/';
        else if (c == L' ') url += L"%20";
        else if (c == L'(') url += L"%28";
        else if (c == L')') url += L"%29";
        else url += c;
    }
    return url;
}

void image(const Sci& s, HWND owner, const std::wstring& docDir, UINT cp) {
    std::wstring file = pickImage(owner);
    if (file.empty()) return;
    std::wstring name = PathFindFileNameW(file.c_str());
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos) name = name.substr(0, dot);
    Sci_Position a = s(SCI_GETSELECTIONSTART), b = s(SCI_GETSELECTIONEND);
    std::string alt = range(s, a, b);
    if (alt.empty()) alt = encode(name, cp);
    put(s, a, b, "![" + std::string(1, kSel) + alt + kSel + "](" + encode(imagePath(file, docDir), cp) + ")");
}

// Przypis: znacznik w miejscu kursora, treść na końcu dokumentu.
void footnote(const Sci& s, const std::string& placeholder) {
    Sci_Position len = s(SCI_GETLENGTH);
    std::string all = range(s, 0, len);
    int next = 1;
    for (size_t p = all.find("[^"); p != std::string::npos; p = all.find("[^", p + 2)) {
        int n = atoi(all.c_str() + p + 2);
        if (n >= next) next = n + 1;
    }
    std::string mark = "[^" + std::to_string(next) + "]";
    std::string e = eol(s);
    Sci_Position b = s(SCI_GETSELECTIONEND);

    bool endsWithEol = len > 0 && (all.back() == '\n' || all.back() == '\r');
    std::string tail = (endsWithEol ? "" : e) + e + mark + ": " + placeholder + e;
    replaceRange(s, len, len, tail);
    replaceRange(s, b, b, mark);
    s(SCI_GOTOPOS, b + mark.size());
}

// Wyczyść formatowanie: znaczniki w linii oraz nagłówki i cytaty na początku linii (listy zostają).
void clearFormatting(const Sci& s) {
    Sci_Position a = s(SCI_GETSELECTIONSTART), b = s(SCI_GETSELECTIONEND);
    if (a == b) {
        Sci_Position ln = s(SCI_LINEFROMPOSITION, a);
        a = s(SCI_POSITIONFROMLINE, ln);
        b = s(SCI_GETLINEENDPOSITION, ln);
    }
    bool lineStart = a == s(SCI_POSITIONFROMLINE, s(SCI_LINEFROMPOSITION, a));
    std::string in = range(s, a, b), out;
    for (size_t i = 0; i < in.size();) {
        bool atLine = (i == 0 && lineStart) || (i > 0 && in[i - 1] == '\n');
        if (atLine) {
            size_t sp = in.find_first_not_of(" \t", i);
            if (sp == std::string::npos) sp = in.size();
            std::string rest = in.substr(sp, 8);
            size_t cut = headingLen(rest);
            if (!cut) cut = quoteLen(rest);
            if (cut) { out.append(in, i, sp - i); i = sp + cut; continue; }
        }
        bool removed = false;
        for (const char* m : {"<kbd>", "</kbd>", "**", "__", "~~", "==", "`"}) {
            size_t n = strlen(m);
            if (in.compare(i, n, m) == 0) { i += n; removed = true; break; }
        }
        if (removed) continue;
        // Pojedyncza gwiazdka to kursywa - chyba że to punkt listy "* ".
        if (in[i] == '*' && !(atLine && i + 1 < in.size() && in[i + 1] == ' ')) { i++; continue; }
        out += in[i++];
    }
    put(s, a, b, kSel + out + kSel);
}

// Punkt listy w linii: szerokość wcięcia (tab = 4), długość znacznika, rodzaj i numer.
struct Item {
    bool ok = false;
    int indent = 0;           // kolumny
    size_t indentBytes = 0;   // bajty wcięcia w linii
    size_t marker = 0;
    std::string type, text;
    int number = 0;
    char delim = '.';
};

Item itemAt(const Sci& s, Sci_Position line) {
    Item it;
    it.text = range(s, s(SCI_POSITIONFROMLINE, line), s(SCI_GETLINEENDPOSITION, line));
    while (it.indentBytes < it.text.size() && (it.text[it.indentBytes] == ' ' || it.text[it.indentBytes] == '\t')) {
        it.indent += it.text[it.indentBytes] == '\t' ? 4 - it.indent % 4 : 1;
        it.indentBytes++;
    }
    std::string body = it.text.substr(it.indentBytes);
    it.marker = listLen(body, &it.type);
    it.ok = it.marker > 0;
    if (it.ok && it.type == "ol") {
        it.number = atoi(body.c_str());
        it.delim = body[body.find_first_not_of("0123456789")];
    }
    return it;
}

// Najbliższy punkt listy powyżej o wcięciu spełniającym warunek; zatrzymuje się na zwykłym tekście bez wcięcia.
template <class Pred>
Sci_Position findAbove(const Sci& s, Sci_Position line, Pred pred, Item* out) {
    for (Sci_Position ln = line - 1; ln >= 0; ln--) {
        Item it = itemAt(s, ln);
        if (it.text.find_first_not_of(" \t") == std::string::npos) continue;
        if (!it.ok) {
            if (it.indent == 0) return -1;
            continue;   // dalszy ciąg tekstu punktu
        }
        if (pred(it)) { *out = it; return ln; }
    }
    return -1;
}

// Numer następnego punktu listy numerowanej na danym poziomie wcięcia; 0 = wyżej nie ma takiej listy (nowa lista).
int numberAfter(const Sci& s, Sci_Position line, int indent) {
    Item prev;
    if (findAbove(s, line, [&](const Item& it) { return it.indent <= indent; }, &prev) < 0) return 0;
    return prev.indent == indent && prev.type == "ol" ? prev.number + 1 : 0;
}

// Poprawia numery w dalszej części listy (po wstawieniu, usunięciu albo przeniesieniu punktu).
// Pierwszy punkt listy zachowuje swój numer (lista może celowo zaczynać się od 5), chyba że leży w linii startAt.
void renumberList(const Sci& s, Sci_Position from, Sci_Position startAt) {
    Sci_Position count = s(SCI_GETLINECOUNT);
    for (Sci_Position ln = from; ln < count && ln < from + 1000; ln++) {
        Item it = itemAt(s, ln);
        if (it.text.find_first_not_of(" \t") == std::string::npos) {
            if (ln + 1 < count && it.text.empty() && itemAt(s, ln + 1).indent == 0 && !itemAt(s, ln + 1).ok) break;
            continue;
        }
        if (!it.ok) {
            if (it.indent == 0) break;
            continue;
        }
        if (it.type != "ol") continue;
        int want = numberAfter(s, ln, it.indent);
        if (want == 0) {
            if (ln != startAt) continue;
            want = 1;
        }
        if (want == it.number) continue;
        Sci_Position st = s(SCI_POSITIONFROMLINE, ln) + (Sci_Position)it.indentBytes;
        size_t digits = it.text.find_first_not_of("0123456789", it.indentBytes) - it.indentBytes;
        replaceRange(s, st, st + (Sci_Position)digits, std::to_string(want));
    }
}

// Czy linia leży wewnątrz bloku kodu ``` / ~~~ (tam listy nie działają).
bool inFence(const Sci& s, Sci_Position line) {
    bool open = false;
    for (Sci_Position ln = std::max<Sci_Position>(0, line - 20000); ln < line; ln++) {
        std::string t = range(s, s(SCI_POSITIONFROMLINE, ln), s(SCI_GETLINEENDPOSITION, ln));
        size_t i = t.find_first_not_of(" \t");
        if (i != std::string::npos && i < 4 && (t.compare(i, 3, "```") == 0 || t.compare(i, 3, "~~~") == 0)) open = !open;
    }
    return open;
}

// ----- Tabele -----

std::string trimmed(const std::string& t) {
    size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
}

bool blankLine(const std::string& t) { return t.find_first_not_of(" \t") == std::string::npos; }

// Liczba znaków | rozdzielających komórki (bez \| i bez | wewnątrz `kodu`).
template <class F>
void forEachPipe(const std::string& t, F f) {
    bool code = false;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '\\') { i++; continue; }
        if (t[i] == '`') code = !code;
        else if (t[i] == '|' && !code) f(i);
    }
}

std::vector<std::string> splitRow(const std::string& line) {
    std::string t = trimmed(line);
    std::vector<std::string> cells;
    size_t start = 0;
    forEachPipe(t, [&](size_t i) { cells.push_back(trimmed(t.substr(start, i - start))); start = i + 1; });
    cells.push_back(trimmed(t.substr(start)));
    if (!t.empty() && t[0] == '|' && cells.size() > 1) cells.erase(cells.begin());
    if (cells.size() > 1 && t.size() > 1 && t.back() == '|' && cells.back().empty()) cells.pop_back();
    return cells;
}

bool isDelimRow(const std::string& line, std::vector<char>* align = nullptr) {
    if (line.find('-') == std::string::npos) return false;
    std::vector<char> out;
    for (const auto& c : splitRow(line)) {
        bool l = !c.empty() && c[0] == ':', r = c.size() > 1 && c.back() == ':';
        if (c.size() < (size_t)(1 + l + r)) return false;
        std::string mid = c.substr(l, c.size() - l - r);
        if (mid.empty() || mid.find_first_not_of('-') != std::string::npos) return false;
        out.push_back(l && r ? 'c' : r ? 'r' : l ? 'l' : 'n');
    }
    if (align) *align = out;
    return !out.empty();
}

// Szerokość tekstu w kolumnach edytora (znaki łączące 0, CJK i emoji 2).
int displayWidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        unsigned cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3f);
        i += n;
        if ((cp >= 0x300 && cp <= 0x36f) || cp == 0x200d || (cp >= 0xfe00 && cp <= 0xfe0f)) continue;
        bool wide = (cp >= 0x1100 && cp <= 0x115f) || (cp >= 0x2e80 && cp <= 0xa4cf) || (cp >= 0xac00 && cp <= 0xd7a3) ||
                    (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xfe30 && cp <= 0xfe4f) || (cp >= 0xff00 && cp <= 0xff60) ||
                    (cp >= 0xffe0 && cp <= 0xffe6) || (cp >= 0x1f300 && cp <= 0x1faff) || (cp >= 0x20000 && cp <= 0x3fffd);
        w += wide ? 2 : 1;
    }
    return w;
}

struct Table {
    std::string indent;
    std::vector<std::vector<std::string>> rows;   // wiersz 1 = linia z kreskami (pusty)
    std::vector<char> align;
    size_t cols = 0;
};

// lines[1] musi być linią z kreskami (| --- | :-: |).
bool parseTable(const std::vector<std::string>& lines, Table* t) {
    if (lines.size() < 2 || !isDelimRow(lines[1], &t->align)) return false;
    t->indent = lines[0].substr(0, lines[0].find_first_not_of(" \t"));
    t->cols = t->align.size();
    t->rows.clear();
    for (size_t i = 0; i < lines.size(); i++) {
        t->rows.push_back(i == 1 ? std::vector<std::string>() : splitRow(lines[i]));
        t->cols = std::max(t->cols, t->rows.back().size());
    }
    for (auto& r : t->rows) r.resize(t->cols);
    t->align.resize(t->cols, 'n');
    return true;
}

// Wyrównana tabela; cells[r][c] = {początek treści komórki w linii, długość}.
std::vector<std::string> formatTable(const Table& t, std::vector<std::vector<std::pair<size_t, size_t>>>* cells = nullptr) {
    std::vector<int> w(t.cols, 3);
    for (size_t r = 0; r < t.rows.size(); r++) {
        if (r == 1) continue;
        for (size_t c = 0; c < t.cols; c++) w[c] = std::max(w[c], displayWidth(t.rows[r][c]));
    }
    std::vector<std::string> out;
    if (cells) cells->assign(t.rows.size(), {});
    for (size_t r = 0; r < t.rows.size(); r++) {
        std::string line = t.indent + "|";
        for (size_t c = 0; c < t.cols; c++) {
            char a = t.align[c];
            if (r == 1) {
                line += ' ';
                line += (a == 'l' || a == 'c') ? ":" : "";
                line += std::string(w[c] - (a == 'c' ? 2 : a == 'n' ? 0 : 1), '-');
                line += (a == 'r' || a == 'c') ? ":" : "";
                line += " |";
                continue;
            }
            const std::string& cell = t.rows[r][c];
            int pad = w[c] - displayWidth(cell), left = a == 'r' ? pad : a == 'c' ? pad / 2 : 0;
            line += ' ' + std::string(left, ' ');
            if (cells) (*cells)[r].push_back({line.size(), cell.size()});
            line += cell + std::string(pad - left, ' ') + " |";
        }
        out.push_back(line);
    }
    return out;
}

std::string lineText(const Sci& s, Sci_Position ln) { return range(s, s(SCI_POSITIONFROMLINE, ln), s(SCI_GETLINEENDPOSITION, ln)); }

bool tableRowLine(const std::string& t) {
    size_t i = t.find_first_not_of(" \t");
    return i != std::string::npos && i < 4 && t.find('|') != std::string::npos;
}

bool tableAt(const Sci& s, Sci_Position line, Table* t, Sci_Position* first, Sci_Position* last) {
    if (!tableRowLine(lineText(s, line)) || inFence(s, line)) return false;
    Sci_Position count = s(SCI_GETLINECOUNT), a = line, b = line;
    while (a > 0 && tableRowLine(lineText(s, a - 1))) a--;
    while (b + 1 < count && tableRowLine(lineText(s, b + 1))) b++;
    // Akapit z | tuż nad tabelą nie należy do niej: szukamy linii z kreskami.
    while (a + 1 <= b && !isDelimRow(lineText(s, a + 1))) a++;
    if (a + 1 > b || line < a) return false;
    std::vector<std::string> lines;
    for (Sci_Position ln = a; ln <= b; ln++) lines.push_back(lineText(s, ln));
    *first = a;
    *last = b;
    return parseTable(lines, t);
}

std::string joinLines(const std::vector<std::string>& lines, const std::string& e) {
    std::string out;
    for (size_t i = 0; i < lines.size(); i++) out += (i ? e : "") + lines[i];
    return out;
}

// ----- Porządki -----

std::string rstrip(const std::string& t) {
    size_t e = t.find_last_not_of(" \t");
    return e == std::string::npos ? std::string() : t.substr(0, e + 1);
}

bool isAtxHeading(const std::string& t) {
    size_t i = t.find_first_not_of(' ');
    if (i == std::string::npos || i > 3) return false;
    return headingLen(t.substr(i)) > 0;
}

bool isRule(const std::string& t) {
    std::string x = trimmed(t);
    if (x.size() < 3 || (x[0] != '*' && x[0] != '-' && x[0] != '_')) return false;
    size_t marks = 0;
    for (char c : x) {
        if (c == x[0]) marks++;
        else if (c != ' ' && c != '\t') return false;
    }
    return marks >= 3;
}

std::string tidyText(const std::string& all, const std::string& e) {
    std::vector<std::string> in;
    size_t start = 0;
    for (size_t i = 0; i <= all.size(); i++) {
        if (i == all.size() || all[i] == '\n') {
            std::string ln = all.substr(start, i - start);
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            in.push_back(ln);
            start = i + 1;
        }
    }
    std::vector<std::string> out;
    size_t i = 0;
    // Nagłówek YAML bez zmian.
    if (!in.empty() && rstrip(in[0]) == "---") {
        for (size_t j = 1; j < in.size(); j++) {
            std::string x = rstrip(in[j]);
            if (x == "---" || x == "...") {
                for (; i <= j; i++) out.push_back(rstrip(in[i]));
                break;
            }
        }
    }
    bool fence = false;
    char fenceCh = 0;
    for (; i < in.size(); i++) {
        const std::string& ln = in[i];
        size_t ind = ln.find_first_not_of(" \t");
        bool fenceLine = ind != std::string::npos && ind < 4 && (ln.compare(ind, 3, "```") == 0 || ln.compare(ind, 3, "~~~") == 0);
        if (fence) {
            out.push_back(ln);   // wnętrze bloku kodu bez zmian
            if (fenceLine && ln[ind] == fenceCh) fence = false;
            continue;
        }
        if (fenceLine) { fence = true; fenceCh = ln[ind]; }
        std::string t = rstrip(ln);
        if (t.empty()) {
            if (!out.empty() && !blankLine(out.back())) out.push_back("");
            continue;
        }
        // Twarde złamanie linii (dwie spacje na końcu) zostaje.
        bool hard = !fenceLine && ln.size() >= t.size() + 2 && ln.compare(t.size(), 2, "  ") == 0 && i + 1 < in.size() && !blankLine(in[i + 1]) &&
                    !isAtxHeading(in[i + 1]);
        if (hard && !isAtxHeading(t)) t += "  ";
        if (!fenceLine && (t[ind] == '*' || t[ind] == '+') && ind + 1 < t.size() && t[ind + 1] == ' ' && !isRule(t)) t[ind] = '-';

        if (!fenceLine && ind < 4 && t.find('|') != std::string::npos && i + 1 < in.size() && isDelimRow(in[i + 1])) {
            std::vector<std::string> rows;
            size_t j = i;
            while (j < in.size() && tableRowLine(in[j])) rows.push_back(rstrip(in[j++]));
            Table tb;
            if (parseTable(rows, &tb)) {
                for (auto& x : formatTable(tb)) out.push_back(x);
                i = j - 1;
                continue;
            }
        }
        if (!fenceLine && isAtxHeading(t)) {
            if (!out.empty() && !blankLine(out.back())) out.push_back("");
            out.push_back(t);
            if (i + 1 < in.size() && !blankLine(in[i + 1])) out.push_back("");
            continue;
        }
        out.push_back(t);
    }
    while (!out.empty() && blankLine(out.back())) out.pop_back();
    return joinLines(out, e) + e;
}

} // namespace

namespace {

// Jedno zwykłe zaznaczenie w obrębie linii (bez zaznaczeń prostokątnych i wielokrotnych).
bool simpleSelection(const Sci& s, Sci_Position* a, Sci_Position* b) {
    *a = s(SCI_GETSELECTIONSTART);
    *b = s(SCI_GETSELECTIONEND);
    return *a < *b && s(SCI_GETSELECTIONS) == 1 && s(SCI_GETSELECTIONMODE) == SC_SEL_STREAM &&
           s(SCI_LINEFROMPOSITION, *a) == s(SCI_LINEFROMPOSITION, *b) && !s(SCI_GETREADONLY);
}

} // namespace

bool wrapSelectionWith(HWND sciHwnd, wchar_t ch) {
    Sci s{sciHwnd};
    Sci_Position a, b;
    if (!simpleSelection(s, &a, &b)) return false;
    std::string mark(1, (char)ch);
    s(SCI_BEGINUNDOACTION);
    put(s, a, b, mark + kSel + range(s, a, b) + kSel + mark);
    s(SCI_ENDUNDOACTION);
    return true;
}

bool pasteLinkOverSelection(HWND sciHwnd) {
    Sci s{sciHwnd};
    Sci_Position a, b;
    if (!simpleSelection(s, &a, &b) || !OpenClipboard(sciHwnd)) return false;
    std::wstring clip;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) { clip = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    while (!clip.empty() && iswspace(clip.back())) clip.pop_back();
    while (!clip.empty() && iswspace(clip.front())) clip.erase(0, 1);
    bool url = (clip.rfind(L"http://", 0) == 0 || clip.rfind(L"https://", 0) == 0 || clip.rfind(L"mailto:", 0) == 0) &&
               clip.find_first_of(L" \t\r\n") == std::wstring::npos;
    if (!url) return false;
    UINT cp = (UINT)s(SCI_GETCODEPAGE);
    s(SCI_BEGINUNDOACTION);
    put(s, a, b, "[" + range(s, a, b) + "](" + encode(clip, cp ? cp : CP_ACP) + ")");
    s(SCI_ENDUNDOACTION);
    return true;
}

void renumberFrom(HWND sciHwnd, Sci_Position line, bool afterDelete) {
    Sci s{sciHwnd};
    if (s(SCI_GETREADONLY) || inFence(s, line)) return;
    s(SCI_BEGINUNDOACTION);
    line = std::max<Sci_Position>(0, line);
    renumberList(s, line, afterDelete ? line : -1);
    s(SCI_ENDUNDOACTION);
}

bool indentListItem(HWND sciHwnd, bool outdent) {
    Sci s{sciHwnd};
    Sci_Position a = s(SCI_GETSELECTIONSTART), b = s(SCI_GETSELECTIONEND);
    Sci_Position line = s(SCI_LINEFROMPOSITION, a);
    if (s(SCI_LINEFROMPOSITION, b) != line || s(SCI_GETREADONLY)) return false;
    Item cur = itemAt(s, line);
    if (!cur.ok || inFence(s, line)) return false;

    int newIndent = cur.indent;
    std::string body = cur.text.substr(cur.indentBytes);
    std::string marker = body.substr(0, cur.marker);
    Item ref;
    if (!outdent) {
        // Podpunkt możliwy tylko pod poprzednim punktem z tego samego poziomu.
        if (findAbove(s, line, [&](const Item& it) { return it.indent <= cur.indent; }, &ref) < 0 || ref.indent != cur.indent) return true;
        newIndent = ref.indent + (int)ref.marker;
        if (cur.type == "ol") marker = std::string("1") + cur.delim + " ";
    } else {
        if (cur.indent == 0) return true;
        if (findAbove(s, line, [&](const Item& it) { return it.indent < cur.indent; }, &ref) >= 0) {
            newIndent = ref.indent;
            if (cur.type == "ol" && ref.type == "ol") marker = std::to_string(ref.number + 1) + ref.delim + " ";
        } else {
            newIndent = std::max(0, cur.indent - 4);
        }
    }

    Sci_Position st = s(SCI_POSITIONFROMLINE, line);
    std::string head = std::string(newIndent, ' ') + marker;
    Sci_Position oldHead = (Sci_Position)(cur.indentBytes + cur.marker);
    Sci_Position delta = (Sci_Position)head.size() - oldHead;
    s(SCI_BEGINUNDOACTION);
    replaceRange(s, st, st + oldHead, head);
    renumberList(s, line + 1, line + 1);
    s(SCI_ENDUNDOACTION);
    s(SCI_SETSEL, std::max(st + (Sci_Position)head.size(), a + delta), std::max(st + (Sci_Position)head.size(), b + delta));
    return true;
}

void continueList(HWND sciHwnd) {
    Sci s{sciHwnd};
    Sci_Position pos = s(SCI_GETCURRENTPOS), line = s(SCI_LINEFROMPOSITION, pos);
    if (line == 0 || s(SCI_GETSELECTIONEMPTY) == 0 || s(SCI_GETREADONLY)) return;

    // Kursor musi stać na początku treści nowej linii (za ewentualnym wcięciem).
    Sci_Position st = s(SCI_POSITIONFROMLINE, line);
    if (range(s, st, pos).find_first_not_of(" \t") != std::string::npos) return;

    Sci_Position ps = s(SCI_POSITIONFROMLINE, line - 1), pe = s(SCI_GETLINEENDPOSITION, line - 1);
    std::string prev = range(s, ps, pe);
    size_t indent = prev.find_first_not_of(" \t");
    if (indent == std::string::npos || inFence(s, line - 1)) return;
    std::string body = prev.substr(indent), type;
    size_t len = listLen(body, &type);
    if (!len) { len = quoteLen(body); type = len ? "quote" : ""; }
    if (!len) return;

    bool emptyItem = body.find_first_not_of(" \t", len) == std::string::npos;
    if (emptyItem && indent > 0 && type != "quote") {
        // Pusty podpunkt: cofamy go poziom wyżej zamiast kończyć listę (jak w polach czatu).
        s(SCI_BEGINUNDOACTION);
        replaceRange(s, pe, pos, "");
        s(SCI_GOTOPOS, pe);
        s(SCI_ENDUNDOACTION);
        indentListItem(sciHwnd, true);
        s(SCI_GOTOPOS, s(SCI_GETLINEENDPOSITION, line - 1));
        return;
    }

    s(SCI_BEGINUNDOACTION);
    if (emptyItem) {
        // Pusty punkt: koniec listy - zdejmujemy znacznik i wcięcie nowej linii.
        replaceRange(s, st, pos, "");
        replaceRange(s, ps + (Sci_Position)indent, pe, "");
        s(SCI_GOTOPOS, s(SCI_POSITIONFROMLINE, line));
    } else {
        std::string marker;
        if (type == "ol") {
            size_t d = body.find_first_not_of("0123456789");
            marker = std::to_string(atoi(body.c_str()) + 1) + body[d] + " ";
        } else if (type == "task") {
            marker = std::string(1, body[0]) + " [ ] ";
        } else if (type == "quote") {
            marker = "> ";
        } else {
            marker = std::string(1, body[0]) + " ";
        }
        replaceRange(s, pos, pos, marker);
        if (type == "ol") renumberList(s, line + 1, line + 1);
        s(SCI_GOTOPOS, pos + (Sci_Position)marker.size());
    }
    s(SCI_ENDUNDOACTION);
}

void applyEdit(HWND sciHwnd, HWND owner, const std::vector<std::wstring>& f, const std::wstring& docDir) {
    Sci s{sciHwnd};
    if (f.empty() || s(SCI_GETREADONLY)) return;
    UINT cp = (UINT)s(SCI_GETCODEPAGE);
    if (cp == 0) cp = CP_ACP;
    auto arg = [&](size_t i) { return i < f.size() ? encode(f[i], cp) : std::string(); };

    const std::wstring& kind = f[0];
    if (kind == L"undo" || kind == L"redo") {
        s(kind == L"undo" ? SCI_UNDO : SCI_REDO);
        SetFocus(sciHwnd);
        return;
    }

    s(SCI_BEGINUNDOACTION);
    if (kind == L"wrap") wrap(s, arg(1), arg(2), arg(3));
    else if (kind == L"clear") clearFormatting(s);
    else if (kind == L"inline") inlineTemplate(s, arg(1), arg(2));
    else if (kind == L"lines") lines(s, arg(1));
    else if (kind == L"block") block(s, arg(1));
    else if (kind == L"image") image(s, owner, docDir, cp);
    else if (kind == L"footnote") footnote(s, arg(1));
    s(SCI_ENDUNDOACTION);
    s(SCI_SCROLLCARET);
    SetFocus(sciHwnd);
}

bool tableKey(HWND sciHwnd, bool enter, bool back) {
    Sci s{sciHwnd};
    if (s(SCI_GETSELECTIONS) != 1 || s(SCI_GETREADONLY) || s(SCI_AUTOCACTIVE)) return false;
    Sci_Position pos = s(SCI_GETCURRENTPOS), line = s(SCI_LINEFROMPOSITION, pos);
    if (s(SCI_LINEFROMPOSITION, s(SCI_GETANCHOR)) != line) return false;
    Table t;
    Sci_Position first, last;
    if (!tableAt(s, line, &t, &first, &last)) return false;

    // Komórka z kursorem: liczba | przed kursorem (bez początkowego).
    std::string text = lineText(s, line), before = text.substr(0, (size_t)(pos - s(SCI_POSITIONFROMLINE, line)));
    int pipes = 0;
    forEachPipe(before, [&](size_t) { pipes++; });
    bool lead = !trimmed(text).empty() && trimmed(text)[0] == '|';
    size_t r = (size_t)(line - first), c = (size_t)std::max(0, pipes - (lead ? 1 : 0));
    c = std::min(c, t.cols - 1);
    if (r == 1) { r = 0; c = t.cols - 1; }   // linia z kreskami: jak koniec nagłówka

    bool exitTable = false;
    size_t tr = r, tc = c;
    if (enter) {
        bool emptyRow = r >= 2 && std::all_of(t.rows[r].begin(), t.rows[r].end(), [](const std::string& x) { return x.empty(); });
        if (emptyRow && r + 1 == t.rows.size()) {
            t.rows.pop_back();   // pusty ostatni wiersz: koniec tabeli (jak w listach)
            exitTable = true;
        } else {
            tr = std::max<size_t>(r + 1, 2);
            tc = 0;
            t.rows.insert(t.rows.begin() + tr, std::vector<std::string>(t.cols));
        }
    } else if (!back) {
        if (++tc >= t.cols) { tc = 0; tr = tr + 1 == 1 ? 2 : tr + 1; }
        if (tr >= t.rows.size()) t.rows.push_back(std::vector<std::string>(t.cols));
    } else if (tc > 0) {
        tc--;
    } else if (tr > 0) {
        tr = tr - 1 == 1 ? 0 : tr - 1;
        tc = t.cols - 1;
    }

    std::vector<std::vector<std::pair<size_t, size_t>>> cells;
    std::vector<std::string> lines = formatTable(t, &cells);
    if (exitTable) lines.push_back("");
    Sci_Position st = s(SCI_POSITIONFROMLINE, first);
    s(SCI_BEGINUNDOACTION);
    replaceRange(s, st, s(SCI_GETLINEENDPOSITION, last), joinLines(lines, eol(s)));
    s(SCI_ENDUNDOACTION);
    if (exitTable) {
        s(SCI_GOTOPOS, s(SCI_POSITIONFROMLINE, first + (Sci_Position)lines.size() - 1));
    } else {
        Sci_Position ls = s(SCI_POSITIONFROMLINE, first + (Sci_Position)tr);
        auto cell = cells[tr][tc];
        s(SCI_SETSEL, ls + (Sci_Position)cell.first, ls + (Sci_Position)(cell.first + cell.second));
    }
    s(SCI_SCROLLCARET);
    return true;
}

void tidyDocument(HWND sciHwnd) {
    Sci s{sciHwnd};
    if (s(SCI_GETREADONLY)) return;
    Sci_Position len = s(SCI_GETLENGTH);
    std::string all = range(s, 0, len), res = tidyText(all, eol(s));
    if (res == all) return;
    // Podmieniamy tylko fragment, który się zmienił (zakładki i zwinięcia poza nim zostają).
    size_t p = 0, q = 0;
    while (p < all.size() && p < res.size() && all[p] == res[p]) p++;
    while (q < all.size() - p && q < res.size() - p && all[all.size() - 1 - q] == res[res.size() - 1 - q]) q++;
    Sci_Position pos = s(SCI_GETCURRENTPOS), line = s(SCI_LINEFROMPOSITION, pos), col = pos - s(SCI_POSITIONFROMLINE, line);
    Sci_Position top = s(SCI_GETFIRSTVISIBLELINE);
    s(SCI_BEGINUNDOACTION);
    replaceRange(s, (Sci_Position)p, (Sci_Position)(all.size() - q), res.substr(p, res.size() - q - p));
    s(SCI_ENDUNDOACTION);
    line = std::min<Sci_Position>(line, s(SCI_GETLINECOUNT) - 1);
    Sci_Position ls = s(SCI_POSITIONFROMLINE, line);
    s(SCI_GOTOPOS, std::min(ls + col, (Sci_Position)s(SCI_GETLINEENDPOSITION, line)));
    s(SCI_SETFIRSTVISIBLELINE, top);
}

void replaceToc(HWND sciHwnd, const std::wstring& body) {
    Sci s{sciHwnd};
    if (s(SCI_GETREADONLY)) return;
    UINT cp = (UINT)s(SCI_GETCODEPAGE);
    Sci_Position len = s(SCI_GETLENGTH);
    std::string all = range(s, 0, len);
    size_t a = all.find("<!-- toc -->");
    if (a == std::string::npos) return;
    size_t z = all.find("<!-- tocstop -->", a);
    if (z == std::string::npos) return;
    Sci_Position la = s(SCI_LINEFROMPOSITION, a), lz = s(SCI_LINEFROMPOSITION, z);
    if (lz <= la) return;
    Sci_Position from = s(SCI_POSITIONFROMLINE, la + 1), to = s(SCI_POSITIONFROMLINE, lz);
    std::string want = withEol(encode(body, cp ? cp : CP_ACP), eol(s));
    if (range(s, from, to) == want) return;
    Sci_Position caret = s(SCI_GETCURRENTPOS);
    if (caret > from && caret < to) return;   // ktoś pisze w spisie - nie przeszkadzamy
    s(SCI_BEGINUNDOACTION);
    replaceRange(s, from, to, want);
    s(SCI_ENDUNDOACTION);
}

void toggleTask(HWND sciHwnd, Sci_Position line) {
    Sci s{sciHwnd};
    if (s(SCI_GETREADONLY) || line < 0 || line >= s(SCI_GETLINECOUNT)) return;
    Item it = itemAt(s, line);
    if (!it.ok) return;
    size_t box = it.text.find('[', it.indentBytes);
    if (box == std::string::npos || box > it.indentBytes + it.marker + 1 || box + 2 >= it.text.size() || it.text[box + 2] != ']') return;
    char c = it.text[box + 1];
    if (c != ' ' && c != 'x' && c != 'X') return;
    Sci_Position p = s(SCI_POSITIONFROMLINE, line) + (Sci_Position)box + 1;
    s(SCI_BEGINUNDOACTION);
    replaceRange(s, p, p + 1, c == ' ' ? "x" : " ");
    s(SCI_ENDUNDOACTION);
}

void insertText(HWND sciHwnd, const std::wstring& text) {
    Sci s{sciHwnd};
    if (s(SCI_GETREADONLY) || text.empty()) return;
    UINT cp = (UINT)s(SCI_GETCODEPAGE);
    std::string bytes = withEol(encode(text, cp ? cp : CP_ACP), eol(s));
    bytes.erase(std::remove(bytes.begin(), bytes.end(), kSel), bytes.end());
    s(SCI_BEGINUNDOACTION);
    put(s, s(SCI_GETSELECTIONSTART), s(SCI_GETSELECTIONEND), bytes);
    s(SCI_ENDUNDOACTION);
    s(SCI_SCROLLCARET);
}

std::wstring markdownPath(const std::wstring& file, const std::wstring& docDir) { return imagePath(file, docDir); }

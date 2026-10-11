#include "detect.h"
#include <algorithm>
#include <cwctype>

namespace {

constexpr size_t kMaxBytes = 200 * 1024;
constexpr int kMaxLines = 4000;
constexpr int kThreshold = 4;

bool allOf(std::string_view s, std::string_view set) {
    return !s.empty() && s.find_first_not_of(set) == std::string_view::npos;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool isFence(std::string_view u) { return u.starts_with("```") || u.starts_with("~~~"); }

bool isListItem(std::string_view u, bool* task) {
    *task = false;
    if (u.size() > 2 && (u[0] == '-' || u[0] == '*' || u[0] == '+') && u[1] == ' ') {
        auto rest = u.substr(2);
        *task = rest.starts_with("[ ] ") || rest.starts_with("[x] ") || rest.starts_with("[X] ");
        return true;
    }
    size_t d = 0;
    while (d < u.size() && d < 9 && u[d] >= '0' && u[d] <= '9') d++;
    return d > 0 && d + 1 < u.size() && (u[d] == '.' || u[d] == ')') && u[d + 1] == ' ';
}

// Wyniki z wnętrza linii: obrazy, linki, pogrubienia, kod w grawisach.
struct Inline {
    int links = 0, images = 0, bold = 0, code = 0;
    int score = 0;

    void scan(std::string_view l) {
        for (size_t p = l.find("]("); p != std::string_view::npos; p = l.find("](", p + 2)) {
            size_t open = l.rfind('[', p);
            size_t close = l.find(')', p);
            if (open == std::string_view::npos || close == std::string_view::npos || close == p + 2) continue;
            if (open > 0 && l[open - 1] == '!') { if (images++ < 2) score += 3; }
            else if (links++ < 2) score += 2;
        }
        for (std::string_view mark : {"**", "__"}) {
            size_t a = l.find(mark);
            if (a == std::string_view::npos) continue;
            size_t b = l.find(mark, a + 2);
            if (b != std::string_view::npos && b > a + 2 && l[a + 2] != ' ' && bold++ < 2) score += 1;
        }
        size_t a = l.find('`');
        if (a != std::string_view::npos) {
            size_t b = l.find('`', a + 1);
            if (b != std::string_view::npos && b > a + 1 && code++ < 2) score += 1;
        }
    }
};

} // namespace

bool looksLikeMarkdown(std::string_view text) {
    text = text.substr(0, kMaxBytes);
    int score = 0, headings = 0, bullets = 0, quotes = 0, tables = 0;
    bool inFence = false, prevText = false;
    char fenceCh = 0;
    Inline in;

    size_t pos = 0;
    for (int n = 0; pos < text.size() && n < kMaxLines; n++) {
        size_t eol = text.find('\n', pos);
        std::string_view line = text.substr(pos, eol == std::string_view::npos ? text.npos : eol - pos);
        pos = eol == std::string_view::npos ? text.size() : eol + 1;

        std::string_view u = trim(line);
        size_t indent = line.find_first_not_of(' ');
        bool shallow = indent == std::string_view::npos || indent < 4;

        if (inFence) {
            if (u.size() >= 3 && u[0] == fenceCh && allOf(u, std::string(1, fenceCh))) { inFence = false; score += 2; }
            continue;
        }
        if (u.empty()) { prevText = false; continue; }
        if (shallow && isFence(u)) { inFence = true; fenceCh = u[0]; prevText = false; continue; }

        bool structural = false;
        if (shallow) {
            size_t h = 0;
            while (h < u.size() && u[h] == '#') h++;
            bool task = false;
            if (h >= 1 && h <= 6 && h + 1 < u.size() && u[h] == ' ') {
                if (headings++ < 3) score += 2;
                structural = true;
            } else if (prevText && u.size() >= 2 && allOf(u, "=")) {
                score += 2; structural = true;
            } else if (prevText && u.size() >= 3 && allOf(u, "-")) {
                score += 1; structural = true;
            } else if (u.find('|') != std::string_view::npos && u.find("---") != std::string_view::npos && allOf(u, "|:- \t")) {
                if (tables++ < 2) score += 3;
                structural = true;
            } else if (isListItem(u, &task)) {
                if (task) score += 2; else if (bullets++ < 2) score += 1;
                structural = true;
            } else if (u.size() > 2 && u[0] == '>' && u[1] == ' ') {
                if (quotes++ < 2) score += 1;
                structural = true;
            }
        }
        in.scan(u);
        prevText = !structural;
    }
    return score + in.score >= kThreshold;
}

bool hasMarkdownExtension(const std::wstring& path) {
    size_t dot = path.rfind(L'.');
    size_t sep = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) return false;
    std::wstring ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return ext == L"md" || ext == L"markdown" || ext == L"mdown" || ext == L"mkd" || ext == L"mkdn" || ext == L"mdwn";
}

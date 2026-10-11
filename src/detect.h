#pragma once
#include <string>
#include <string_view>

// Heurystyka: czy tekst wygląda na Markdown (działa na niezapisanych dokumentach).
bool looksLikeMarkdown(std::string_view text);

// Rozszerzenia plików Markdown (.md, .markdown, ...).
bool hasMarkdownExtension(const std::wstring& path);

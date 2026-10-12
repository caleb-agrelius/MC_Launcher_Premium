#pragma once

#include <cstddef>
#include <string>

// Turns a pack description written in Markdown (Modrinth, FTB) or HTML (CurseForge,
// some Technic packs) into plain text for the UI: tags, images and link targets are
// dropped (link text is kept), headings and emphasis lose their markers, list items
// start with a middle dot, common entities are decoded, and blank lines are collapsed.
// Characters outside Latin-1 that the UI font lacks (curly quotes, dashes, ellipses)
// become their ASCII forms.
std::string MarkupToPlainText(const std::string& text);

// The first maxLength bytes of text, cut at a word boundary and ending in "...", or text
// itself when it's short enough. Newlines become spaces.
std::string ShortenText(const std::string& text, std::size_t maxLength);

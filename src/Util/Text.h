#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

// Formats large counts compactly: 950, 12.3K, 18.4M.
std::string FormatCount(std::uint64_t count);

// An ISO date or timestamp ("2026-10-07T12:00:00Z") as "Oct 7, 2026", or "" if it isn't one.
std::string FormatShortDate(const std::string& iso);

// A Unix time (seconds) as UTC "YYYY-MM-DD", or "" for 0 or less.
std::string IsoDateFromEpoch(std::int64_t seconds);

// "adventure-and-rpg" or "technology" as "Adventure And Rpg" / "Technology": a category slug for display.
std::string CategoryLabel(const std::string& slug);

// The text in lowercase (ASCII letters only, so UTF-8 stays intact).
std::string ToLower(std::string text);

// True when haystack contains lowerNeedle, ignoring case. lowerNeedle must already be lower case.
bool ContainsIgnoreCase(const std::string& haystack, const std::string& lowerNeedle);

// A byte count as "512 B", "1.4 KB", "23.0 MB" or "1.2 GB".
std::string FormatFileSize(std::uint64_t bytes);

// A play time as "Not played yet", "12 min", "3 h 05 min" or "120 h".
std::string FormatPlaytime(std::int64_t seconds);

// A file time as local "YYYY-MM-DD HH:MM".
std::string FormatFileTime(std::filesystem::file_time_type time);

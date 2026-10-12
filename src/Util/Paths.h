#pragma once

#include <cstdio>
#include <filesystem>

// Windows limits ordinary paths to 260 characters. Pack installs nest config files deeply,
// so file operations go through these helpers:
// LongPath returns the path unchanged when it's short, else its absolute form with the
// "\\?\" prefix that lifts the limit (only honored by wide-character APIs, which
// std::filesystem and OpenFile use). On other systems it returns the path unchanged.
std::filesystem::path LongPath(const std::filesystem::path& path);

// The absolute path with the "\?\" prefix always added (on Windows). Use it for a folder
// that will be walked or copied recursively: paths built under it stay long-path safe even
// when the folder's own path is short.
std::filesystem::path ExtendedPath(const std::filesystem::path& path);

// fopen that accepts long paths (_wfopen on Windows). Returns nullptr on failure.
FILE* OpenFile(const std::filesystem::path& path, const char* mode);

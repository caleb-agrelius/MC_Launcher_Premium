#pragma once

#include <array>
#include <cstdint>
#include <vector>

// The launcher's logo: a pixel-art grass block in isometric view, plus a blocky pixel font
// for the "MC LAUNCHER" wordmark. Plain pixel data, so the window icon, the .ico file and
// the in-app drawing all come from the same source.
namespace Logo {

// The block's native size in pixels. Larger icons scale it up without smoothing.
constexpr int kBaseSize = 32;

// size x size RGBA pixels (row-major, top row first), transparent around the block.
// Sizes below kBaseSize are averaged down; larger ones use whole-pixel scaling, centered.
std::vector<std::uint8_t> BlockPixels(int size);

// One 5x7 glyph of the wordmark font: 7 rows, bit 4 = leftmost column. Unknown characters
// (and space) are blank.
std::array<std::uint8_t, 7> Glyph(char c);
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;

}

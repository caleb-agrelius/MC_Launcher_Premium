#pragma once

#include "imgui.h"

#include <cstdint>
#include <string>

struct LoadedImage;
struct Modpack;
class ImageCache;

// Launcher-specific drawing helpers built on ImGui. Sizes are design-time pixels
// and are scaled with Theme::S().
namespace Widgets {

// Text in a given font, size (unscaled, see Theme::kFont*) and color.
void Text(ImFont* font, float size, const ImVec4& color, const char* text);
// Small uppercase-style section heading, e.g. "PACK FILES".
void SectionLabel(const char* text);
// A spinning arc sized to the current text line.
void Spinner(const ImVec4& color);
// A rounded "pill" tag, laid out like a normal item (use SameLine() between chips).
void Chip(const char* text, const ImVec4& tint);
// Thin accent progress bar spanning the available width.
void ThinProgressBar(float fraction);

// Sidebar navigation entry with an accent bar when selected. Returns true when clicked.
bool NavItem(const char* label, bool selected);

// Opacity for an image that just finished loading (fades in over a quarter second).
float FadeAlpha(const LoadedImage& image);

// Pack icon: the loaded image, or a tile in the pack's color with its first letter.
void DrawPackIcon(ImDrawList* drawList, const LoadedImage* image, const Modpack& pack,
				  ImVec2 min, float size, float rounding);

// Selectable row for the pack list: icon, name, and a metadata line. Returns true when clicked.
bool PackRow(const Modpack& pack, ImageCache& images, bool selected, const std::string& meta);

// Wide banner for the selected pack: gallery image (or a pattern in the pack's color),
// a gradient into the panel color, and the icon, name, author and tags on top.
void HeroBanner(const Modpack& pack, ImageCache& images, float height);

}

#pragma once

#include "imgui.h"

#include <cstdint>
#include <string>

struct LoadedImage;
struct Modpack;
class ImageCache;
namespace Theme { struct ThemeInfo; }

// Launcher-specific drawing helpers built on ImGui. Sizes are design-time pixels
// and are scaled with Theme::S().
namespace Widgets {

// When true, images appear without fading in.
void SetReduceMotion(bool reduce);

// Text in a given font, size (unscaled, see Theme::kFont*) and color.
void Text(ImFont* font, float size, const ImVec4& color, const char* text);
// Small uppercase-style section heading, e.g. "PACK FILES".
void SectionLabel(const char* text);
// A spinning arc sized to the current text line.
void Spinner(const ImVec4& color);
// A rounded "pill" tag, laid out like a normal item (use SameLine() between chips).
void Chip(const char* text, const ImVec4& tint);
// On/off switch in the theme's colors. Returns true when toggled.
bool Toggle(const char* id, bool* value);
// Thin accent progress bar spanning the available width.
void ThinProgressBar(float fraction);

// Sidebar navigation entry with an accent bar when selected, and an optional count
// badge (shown when badge > 0). Returns true when clicked.
bool NavItem(const char* label, bool selected, int badge = 0);

// Opacity for an image that just finished loading (fades in over a quarter second).
float FadeAlpha(const LoadedImage& image);

// Pack icon: the loaded image, or a tile in the pack's color with its first letter.
void DrawPackIcon(ImDrawList* drawList, const LoadedImage* image, const Modpack& pack,
				  ImVec2 min, float size, float rounding);

// Selectable row for the pack list: icon, name, and a metadata line. Returns true when clicked.
bool PackRow(const Modpack& pack, ImageCache& images, bool selected, const std::string& meta);

// Wide banner for the selected pack: gallery image (or a pattern in the pack's color),
// a gradient into the panel color, and the icon, name, author and tags on top.
// With showGallery false, only the pattern is drawn and no gallery images are loaded.
void HeroBanner(const Modpack& pack, ImageCache& images, float height, bool showGallery = true);

// Centered placeholder for empty pages: a block illustration, a title, a hint, and an
// optional button. Returns true when the button is clicked.
bool EmptyState(const char* title, const char* body, const char* buttonLabel = nullptr);

// Clickable theme swatch: a miniature launcher in the theme's colors plus its name.
// Returns true when clicked.
bool ThemeCard(const Theme::ThemeInfo& theme, bool selected, float width);

}

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

// A bordered card in the card color that is as tall as its contents. Pair with EndCard().
// The height is measured each frame and remembered (per id, in the parent window), so
// cards don't collapse and re-grow when a page is shown again. Cards never take the
// mouse wheel: scrolling over one scrolls the page around it.
// padding < 0 uses the style's window padding.
void BeginCard(const char* id, ImVec2 padding = ImVec2(-1.0f, -1.0f));
void EndCard();

// When true, images appear without fading in.
void SetReduceMotion(bool reduce);

// Entrance animation: the item fades in and rises into place. start is the
// ImGui::GetTime() the animation began (e.g. when the page was opened); index staggers
// list items so they "bubble" in one after another. Pair each call with EndEntrance().
// Without motion (reduce motion, or once finished) it changes nothing.
void BeginEntrance(double start, int index = 0);
void EndEntrance();

// Mouse-wheel scrolling that eases toward its target instead of jumping. Call just
// before EndChild() of a child created with ImGuiWindowFlags_NoScrollWithMouse. The
// innermost scrollable window under the mouse takes the wheel. Scrollbar drags and
// keyboard navigation keep working.
void SmoothScroll();

// Text in a given font, size (unscaled, see Theme::kFont*) and color.
void Text(ImFont* font, float size, const ImVec4& color, const char* text);
// Colored text that wraps at the edge of the window.
void WrappedText(const ImVec4& color, const char* text);
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

// A text row for lists and combo popups, with a rounded hover/selection highlight.
// Returns true when clicked.
bool ListItem(const char* label, bool selected);
// Sidebar navigation entry with an accent bar when selected, and an optional count
// badge (shown when badge > 0). Returns true when clicked.
bool NavItem(const char* label, bool selected, int badge = 0);
// A NavItem that heads a collapsible group of SubNavItems, with a chevron pointing
// down when expanded and right when collapsed. Returns true when clicked.
bool NavGroupItem(const char* label, bool selected, bool expanded);
// Smaller, indented entry under a NavItem (e.g. each Discover source). Returns true when clicked.
bool SubNavItem(const char* label, bool selected);

// Opacity for an image that just finished loading (fades in over a quarter second).
float FadeAlpha(const LoadedImage& image);

// Pack icon: the loaded image, or a tile in the pack's color with its first letter.
void DrawPackIcon(ImDrawList* drawList, const LoadedImage* image, const Modpack& pack,
				  ImVec2 min, float size, float rounding);

// Selectable row for the pack list: icon, name, and a metadata line. Returns true when clicked.
// tag (optional) is drawn as a small pill in tagColor before the metadata, e.g. the source.
bool PackRow(const Modpack& pack, ImageCache& images, bool selected, const std::string& meta,
	const char* tag = nullptr, const ImVec4& tagColor = ImVec4(0, 0, 0, 0));

// A pill-shaped on/off filter button with a colored dot. Returns true when clicked.
bool ToggleChip(const char* label, bool on, const ImVec4& color, bool enabled = true);

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
// Dashed "Create your own" card the same size as ThemeCard. Returns true when clicked.
bool NewThemeCard(float width);

}

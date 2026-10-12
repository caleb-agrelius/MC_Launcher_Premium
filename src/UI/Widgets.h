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
// padding < 0 uses the style's window padding. width <= 0 fills the available width.
void BeginCard(const char* id, ImVec2 padding = ImVec2(-1.0f, -1.0f), float width = 0.0f);
void EndCard();

// When true, images appear without fading in.
void SetReduceMotion(bool reduce);
// How fast animations play: 1 is the normal speed, 2 twice as fast. Divide a duration by it, or multiply a
// rate. Settings > Appearance > Animation speed.
void SetAnimationSpeed(float speed);
float AnimationSpeed();

// Entrance animation: the item fades in and rises into place. start is the
// ImGui::GetTime() the animation began (e.g. when the page was opened); index staggers
// list items so they "bubble" in one after another. Pair each call with EndEntrance().
// Without motion (reduce motion, or once finished) it changes nothing.
void BeginEntrance(double start, int index = 0);
void EndEntrance();

// Mouse-wheel scrolling that eases toward its target instead of jumping. Call just
// before EndChild() of a child created with ImGuiWindowFlags_NoScrollWithMouse. The
// innermost scrollable window under the mouse takes the wheel, wherever the mouse is
// inside it (over cards, text or empty space); a popup under the mouse (a combo's list)
// keeps the wheel for itself. Scrollbar drags and keyboard navigation keep working.
void SmoothScroll();
// A child window that scrolls with SmoothScroll. Always pair with EndScrollArea(), whatever
// BeginScrollArea returns (like BeginChild/EndChild).
bool BeginScrollArea(const char* id, ImVec2 size = ImVec2(0.0f, 0.0f), ImGuiChildFlags childFlags = 0,
	ImGuiWindowFlags windowFlags = 0);
void EndScrollArea();

// A small "x" button with a rounded hover highlight at a screen position. Returns true when clicked.
bool CloseX(const char* id, ImVec2 min, float size);
// Replaces the title bar's close button (which highlights with a circle) in a modal opened without
// a p_open. Call right after BeginPopupModal; returns true when clicked.
bool ModalCloseButton();

// A draggable divider between two side-by-side panes; put it between them with
// SameLine(0, 0) on both sides. Dragging changes *width (the left pane's width, in
// unscaled pixels), kept within [minWidth, maxWidth]. Returns true while it changes.
bool PaneSplitter(const char* id, float* width, float minWidth, float maxWidth);

// Text in a given font, size (unscaled, see Theme::kFont*) and color.
void Text(ImFont* font, float size, const ImVec4& color, const char* text);
// Colored text that wraps at the edge of the window.
void WrappedText(const ImVec4& color, const char* text);
// Small uppercase-style section heading, e.g. "PACK FILES".
void SectionLabel(const char* text);
// A spinning arc sized to the current text line.
void Spinner(const ImVec4& color);
// A pulsing rounded bar standing in for content that is still loading, so the page keeps the size
// it will have. Still (no pulse) with Reduce motion on.
// barHeight > 0 draws a thinner bar centered in size (for text next to taller controls).
void Skeleton(ImVec2 size, float rounding = 6.0f, float barHeight = 0.0f);
// Pads the layout so that everything since startCursorY (the cursor's Y when the block began) is at least
// minHeight tall. Use it where a block is shorter while loading than once loaded, so what follows
// doesn't move.
void PadToHeight(float startCursorY, float minHeight);
// A frame-high field with a spinner and label inside, standing in for a dropdown or input that is
// still loading. width <= 0 uses the next item width (like a real input).
void LoadingField(const char* label, float width = 0.0f);
// A rounded "pill" tag, laid out like a normal item (use SameLine() between chips).
void Chip(const char* text, const ImVec4& tint);
// The amber "Modified" chip for a pack version the player changed, with what changed
// (e.g. "2 added, 1 turned off") in its tooltip. Used wherever an installed version shows.
void ModifiedChip(const std::string& changes);
// The color of ModifiedChip, for text that says a pack is modified.
ImVec4 ModifiedColor();
// On/off switch in the theme's colors. Returns true when toggled.
bool Toggle(const char* id, bool* value);
// A row of pill-shaped tabs; the selected one is filled. Sets *current and returns true
// when another tab is clicked.
bool TabStrip(const char* id, const char* const* labels, int count, int* current);
// Thin accent progress bar spanning the available width.
void ThinProgressBar(float fraction);

// A text row for lists and dropdowns, with a rounded hover/selection highlight. Returns
// true when clicked; in a popup the click also closes it unless closePopup is false.
// hint (optional) is dimmed text on the right, e.g. "Minecraft 1.20.1" or a shortcut.
// In a popup the row asks for room for both, so the popup widens to fit them.
// textColor, when set, replaces the label's color (e.g. green for an installed version).
bool ListItem(const char* label, bool selected, bool closePopup = true, const char* hint = nullptr,
	const ImVec4* textColor = nullptr);
// A popup menu entry (context menus, the account menu): a ListItem with an optional
// shortcut hint on the right, dimmed and unclickable when disabled.
bool MenuItem(const char* label, const char* shortcut = nullptr, bool enabled = true);

// A dropdown in the theme's soft style: one rounded frame with the choice (shortened with
// "..." if needed) and a chevron, instead of ImGui's square arrow box. Fill it with
// ListItem rows and call EndSelect() only when it returns true. Width is the next item width.
bool BeginSelect(const char* id, const char* preview, ImGuiComboFlags flags = 0);
void EndSelect();
// A whole dropdown over fixed labels. Returns true when *current changed.
bool Select(const char* id, int* current, const char* const* labels, int count);

// An on/off switch with a label after it, like a checkbox ("Label##id" hides nothing
// after ##). Returns true when toggled.
bool ToggleRow(const char* label, bool* value);

// Buttons for a page's main action (filled with the accent, e.g. PLAY) and for destructive
// or force actions (red-tinted, e.g. Stop game, Uninstall).
bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));
bool DangerButton(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));
// The width ImGui::Button gives a label with the current font, for right-aligning rows.
float ButtonWidth(const char* label);
// Sidebar navigation entry with an accent bar when selected, and an optional count
// badge (shown when badge > 0). Returns true when clicked.
bool NavItem(const char* label, bool selected, int badge = 0);

// Opacity for an image that just finished loading (fades in over a quarter second).
float FadeAlpha(const LoadedImage& image);

// Pack icon: the loaded image, or a tile in the pack's color with its first letter.
// flags picks which corners are rounded (ImDrawFlags_RoundCorners*; 0 = all).
void DrawPackIcon(ImDrawList* drawList, const LoadedImage* image, const Modpack& pack,
				  ImVec2 min, float size, float rounding, ImDrawFlags flags = 0);

// Selectable row for the pack list: icon, name, and a metadata line. Returns true when clicked.
// tag (optional) is drawn as a small pill in tagColor before the metadata, e.g. the source.
bool PackRow(const Modpack& pack, ImageCache& images, bool selected, const std::string& meta,
	const char* tag = nullptr, const ImVec4& tagColor = ImVec4(0, 0, 0, 0));

// Card for the pack grid, in the style of CurseForge's: a wide cover (the pack's banner image, else a tile in its color) with the source
// on a pill over it, then the pack icon beside the name and "by author" (as on Modrinth), the first category chip and "+N" for the rest, and a
// footer with downloads and the last update (meta stands in when a source has neither).
// width is the card's width; the height follows from it. Returns true when clicked.
bool PackCard(const Modpack& pack, ImageCache& images, bool selected, const std::string& meta,
	const char* tag, const ImVec4& tagColor, float width);

// A pill-shaped on/off filter button with a colored dot. Returns true when clicked.
bool ToggleChip(const char* label, bool on, const ImVec4& color, bool enabled = true);
// A square icon button for switching a view: three stacked lines (grid = false) or a 2x2 grid of
// squares (grid = true), highlighted when on. id must be unique among nearby widgets.
bool ViewModeButton(const char* id, bool grid, bool on);

// Wide banner for the selected pack: gallery image (or a pattern in the pack's color),
// a gradient into the panel color, and the icon, name, author and tags on top.
// With showGallery false, only the pattern is drawn and no gallery images are loaded.
void HeroBanner(const Modpack& pack, ImageCache& images, float height, bool showGallery = true);

// Centered placeholder for empty pages: a block illustration, a title, a hint, and an
// optional button. Returns true when the button is clicked.
bool EmptyState(const char* title, const char* body, const char* buttonLabel = nullptr);

// The launcher logo (the pixel grass block from Logo.h), size x size at the cursor.
void LogoBlock(float size);
// Blocky pixel-font text in the Minecraft title style: each font pixel is pixel x pixel,
// in the face color with a darker extruded edge below and to the right.
void Wordmark(const char* text, float pixel, const ImVec4& face, const ImVec4& depth);

// Clickable theme swatch: a miniature launcher in the theme's colors plus its name.
// Returns true when clicked.
bool ThemeCard(const Theme::ThemeInfo& theme, bool selected, float width);
// Dashed "Create your own" card the same size as ThemeCard. Returns true when clicked.
bool NewThemeCard(float width);

}

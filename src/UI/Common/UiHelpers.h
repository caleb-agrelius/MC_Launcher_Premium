#pragma once

#include "imgui.h"
#include "Network/PackSources.h"

#include <vector>

class ImageCache;
struct Modpack;

// Small layout and label helpers the pages share.
namespace UiHelpers {

// Moves the cursor so a button with this label ends at the right edge of the content region.
void AlignRightForButton(const char* label);

// Moves the cursor so a row of buttons with these labels ends at the right edge.
void AlignRightForButtons(const std::vector<const char*>& labels);

// Draws a pack icon at the cursor and leaves the cursor beside it, ready for SameLine text.
void PackIconItem(const Modpack& pack, ImageCache& images, float size);

// The breadcrumb, title and subtitle at the top of a page. subtitle may be null.
void DrawPageHeader(const char* breadcrumb, const char* title, const char* subtitle);

// A spinner followed by dimmed text, for "Loading..." states.
void BusyLabel(const char* text);

// Dimmed text plus a Refresh button; sets reload when the button is pressed.
void EmptyWithRefresh(const char* message, const char* buttonLabel, bool& reload);

// Each source's brand color, for its filter chip and result tags.
ImVec4 SourceColor(PackSource source);

// The mod loaders Discover can filter by, and their display names.
extern const char* const kLoaderKeys[4];
const char* LoaderLabel(const char* loader);

}

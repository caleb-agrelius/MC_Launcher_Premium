#include "Widgets.h"

#include "ImageCache.h"
#include "Model/Modpack.h"
#include "Theme.h"

#include "imgui_internal.h" // DC.CursorPos, for entrance animations

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace Widgets {

using Theme::S;

static bool g_reduceMotion = false;

void SetReduceMotion(bool reduce)
{
	g_reduceMotion = reduce;
}

static constexpr double kEntranceSeconds = 0.32;
static constexpr double kEntranceStagger = 0.045; // per list item
static constexpr int kEntranceMaxStagger = 10;    // later items don't wait any longer
static std::vector<float> g_entranceOffsets;      // cursor lift per open BeginEntrance

void BeginEntrance(double start, int index)
{
	float progress = 1.0f;
	if (!g_reduceMotion) {
		const double delay = kEntranceStagger * std::min(index, kEntranceMaxStagger);
		const double t = std::clamp((ImGui::GetTime() - start - delay) / kEntranceSeconds, 0.0, 1.0);
		progress = 1.0f - static_cast<float>(std::pow(1.0 - t, 3.0)); // ease-out cubic
	}
	const float offset = (1.0f - progress) * S(14.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * progress);
	// Moves the layout cursor directly: SetCursorPosY would mark it as an explicit
	// move, and ImGui asserts when one isn't followed by an item (see EndEntrance).
	ImGui::GetCurrentWindow()->DC.CursorPos.y += offset;
	g_entranceOffsets.push_back(offset);
}

void EndEntrance()
{
	ImGui::PopStyleVar();
	const float offset = g_entranceOffsets.empty() ? 0.0f : g_entranceOffsets.back();
	if (!g_entranceOffsets.empty())
		g_entranceOffsets.pop_back();
	// Take the lift back out of the layout so items below don't move.
	ImGui::GetCurrentWindow()->DC.CursorPos.y -= offset;
}

static int g_wheelFrame = -1; // the frame whose wheel input a window already used

void SmoothScroll()
{
	ImGuiStorage* storage = ImGui::GetStateStorage();
	const float scroll = ImGui::GetScrollY();
	const float maxScroll = ImGui::GetScrollMaxY();
	float* target = storage->GetFloatRef(ImGui::GetID("##smooth_target"), scroll);
	float* last = storage->GetFloatRef(ImGui::GetID("##smooth_last"), scroll);
	// Scrollbar drags, keyboard navigation and SetScrollHereY move the view on their own.
	if (std::fabs(scroll - *last) > 0.5f)
		*target = scroll;

	// Children end before their parent, so the innermost window gets the wheel first.
	const ImGuiIO& io = ImGui::GetIO();
	const int frame = ImGui::GetFrameCount();
	if (io.MouseWheel != 0.0f && !io.KeyCtrl && g_wheelFrame != frame && maxScroll > 0.0f &&
		ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
		g_wheelFrame = frame;
		*target -= io.MouseWheel * S(120.0f);
	}
	*target = std::clamp(*target, 0.0f, maxScroll);

	float next = *target;
	if (!g_reduceMotion) {
		const float follow = 1.0f - std::exp(-io.DeltaTime * 14.0f); // ~70 ms time constant
		next = scroll + (*target - scroll) * follow;
		if (std::fabs(*target - next) < 0.5f)
			next = *target;
	}
	if (next != scroll)
		ImGui::SetScrollY(next);
	*last = next;
}

// Faint outline for icons and banners: light on dark themes, dark on light ones.
static ImU32 OutlineColor(float alpha)
{
	return Theme::C().light ? IM_COL32(0, 0, 0, static_cast<int>(alpha * 255.0f * 1.5f))
		: IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f));
}

static ImU32 WithAlpha(const ImVec4& color, float alpha)
{
	return ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
}

// The pack's Modrinth color, or the launcher accent if it has none.
static ImVec4 PackColor(const Modpack& pack)
{
	if (pack.color == 0)
		return Theme::C().accentDeep;
	return ImVec4(((pack.color >> 16) & 0xFF) / 255.0f, ((pack.color >> 8) & 0xFF) / 255.0f,
		(pack.color & 0xFF) / 255.0f, 1.0f);
}

static ImVec4 Scaled(const ImVec4& color, float factor)
{
	return ImVec4(color.x * factor, color.y * factor, color.z * factor, color.w);
}

// Draws text clipped to clipMax.x with a trailing "..." if it doesn't fit.
static void DrawClippedText(ImDrawList* drawList, ImFont* font, float size, ImVec2 pos, float maxX,
							ImU32 color, const std::string& text)
{
	const float available = maxX - pos.x;
	if (available <= 0.0f)
		return;
	if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= available) {
		drawList->AddText(font, size, pos, color, text.c_str());
		return;
	}
	const float ellipsisWidth = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "...").x;
	std::size_t length = text.size();
	while (length > 0 &&
		font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str(), text.c_str() + length).x + ellipsisWidth > available)
		--length;
	// Don't cut a UTF-8 sequence in half.
	while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80)
		--length;
	const std::string shortened = text.substr(0, length) + "...";
	drawList->AddText(font, size, pos, color, shortened.c_str());
}

// Draws an image scaled to cover [min, max], cropping the overflow.
static void DrawImageCover(ImDrawList* drawList, const LoadedImage& image, ImVec2 min, ImVec2 max,
						   float rounding, float alpha, ImDrawFlags flags = 0)
{
	const float boxAspect = (max.x - min.x) / std::max(1.0f, max.y - min.y);
	const float imageAspect = static_cast<float>(image.width) / std::max(1, image.height);
	ImVec2 uv0(0.0f, 0.0f);
	ImVec2 uv1(1.0f, 1.0f);
	if (imageAspect > boxAspect) {
		const float visible = boxAspect / imageAspect;
		uv0.x = (1.0f - visible) * 0.5f;
		uv1.x = 1.0f - uv0.x;
	} else {
		const float visible = imageAspect / boxAspect;
		uv0.y = (1.0f - visible) * 0.5f;
		uv1.y = 1.0f - uv0.y;
	}
	drawList->AddImageRounded(image.texture, min, max, uv0, uv1, IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)),
		rounding, flags);
}

// Width of a chip drawn by DrawChip, so callers can lay chips out.
static float ChipWidth(float fontSize, const char* text)
{
	return Theme::GetFonts().bold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text).x + S(20.0f);
}

static void DrawChip(ImDrawList* drawList, ImVec2 pos, float fontSize, const char* text, const ImVec4& tint)
{
	const float width = ChipWidth(fontSize, text);
	const float height = fontSize + S(8.0f);
	drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), WithAlpha(tint, 0.18f), height * 0.5f);
	drawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height), WithAlpha(tint, 0.35f), height * 0.5f);
	// Text in a stronger shade of the tint: lighter on dark themes, darker on light ones.
	const ImVec4 textColor = Theme::C().light ? Scaled(tint, 0.6f)
		: ImVec4(std::min(1.0f, tint.x * 1.25f + 0.1f), std::min(1.0f, tint.y * 1.25f + 0.1f),
			std::min(1.0f, tint.z * 1.25f + 0.1f), 1.0f);
	drawList->AddText(Theme::GetFonts().bold, fontSize, ImVec2(pos.x + S(10.0f), pos.y + S(4.0f)),
		ImGui::GetColorU32(textColor), text);
}

// A clickable row whose hover/selection highlight is a rounded rectangle kept inside the
// content region, unlike ImGui::Selectable, which draws square corners and reaches past
// the item into the window padding. size.x <= 0 means the available width.
static bool RoundedSelectable(const char* id, bool selected, ImVec2 size, float rounding)
{
	const float width = size.x > 0.0f ? size.x : ImGui::GetContentRegionAvail().x;
	const bool pressed = ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, width), std::max(1.0f, size.y)));
	const bool hovered = ImGui::IsItemHovered();
	const bool held = ImGui::IsItemActive();
	if (selected || hovered) {
		const ImGuiCol col = held ? ImGuiCol_HeaderActive : (selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered);
		ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
			ImGui::GetColorU32(col), rounding);
	}
	return pressed;
}

// The selected-row accent: a short rounded bar just inside the row's left edge.
static void DrawSelectionBar(ImDrawList* drawList, ImVec2 min, ImVec2 max, float inset)
{
	const float x = min.x + S(6.0f);
	drawList->AddRectFilled(ImVec2(x, min.y + inset), ImVec2(x + S(4.0f), max.y - inset),
		ImGui::GetColorU32(Theme::C().accent), S(2.0f));
}

bool ListItem(const char* label, bool selected)
{
	ImGui::PushID(label);
	const float height = ImGui::GetTextLineHeight() + S(10.0f);
	const bool clicked = RoundedSelectable("##item", selected, ImVec2(0.0f, height), S(6.0f));
	ImGui::PopID();
	const ImVec2 min = ImGui::GetItemRectMin();
	ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + S(8.0f), min.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
		ImGui::GetColorU32(selected ? Theme::C().text : (ImGui::IsItemHovered() ? Theme::C().text : Theme::C().textDim)), label);
	if (selected && ImGui::IsWindowAppearing())
		ImGui::SetScrollHereY();
	if (clicked)
		ImGui::CloseCurrentPopup(); // like ImGui::Selectable in a combo; does nothing outside a popup
	return clicked;
}

void Text(ImFont* font, float size, const ImVec4& color, const char* text)
{
	ImGui::PushFont(font, size);
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::TextUnformatted(text);
	ImGui::PopStyleColor();
	ImGui::PopFont();
}

void WrappedText(const ImVec4& color, const char* text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}

void SectionLabel(const char* text)
{
	Text(Theme::GetFonts().bold, Theme::kFontSmall, Theme::C().textDim, text);
}

void Spinner(const ImVec4& color)
{
	const float size = ImGui::GetTextLineHeight();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(size, size));
	const float thickness = std::max(1.5f, size * 0.14f);
	const float radius = size * 0.5f - thickness;
	const ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
	const float start = static_cast<float>(ImGui::GetTime()) * 6.0f;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PathArcTo(center, radius, start, start + 3.14159265f * 1.4f, 24);
	drawList->PathStroke(ImGui::GetColorU32(color), 0, thickness);
}

void Chip(const char* text, const ImVec4& tint)
{
	const float fontSize = ImGui::GetFontSize() * (Theme::kFontSmall / Theme::kFontBody);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	DrawChip(ImGui::GetWindowDrawList(), pos, fontSize, text, tint);
	ImGui::Dummy(ImVec2(ChipWidth(fontSize, text), fontSize + S(8.0f)));
}

bool Toggle(const char* id, bool* value)
{
	const float height = ImGui::GetFrameHeight() * 0.8f;
	const float width = height * 1.9f;
	ImVec2 pos = ImGui::GetCursorScreenPos();
	pos.y += (ImGui::GetFrameHeight() - height) * 0.5f; // center on the row like other frames
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, ImGui::GetFrameHeight()));
	if (clicked)
		*value = !*value;
	const bool hovered = ImGui::IsItemHovered();
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

	const Theme::Palette& p = Theme::C();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec4 track = *value ? (hovered ? Theme::Mix(p.accent, p.text, 0.15f) : p.accent)
		: (hovered ? Theme::Mix(p.frameBg, p.textDim, 0.35f) : Theme::Mix(p.frameBg, p.textDim, 0.2f));
	drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(track), height * 0.5f);
	const float knobRadius = height * 0.5f - S(3.0f);
	const float knobX = *value ? pos.x + width - height * 0.5f : pos.x + height * 0.5f;
	const ImVec4 knob = *value ? p.windowBg : p.text;
	drawList->AddCircleFilled(ImVec2(knobX, pos.y + height * 0.5f), knobRadius, ImGui::GetColorU32(knob));
	return clicked;
}

void ThinProgressBar(float fraction)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(4.0f));
	ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-1.0f, S(8.0f)), "");
	ImGui::PopStyleVar();
}

bool NavItem(const char* label, bool selected, int badge)
{
	const float height = S(44.0f);
	ImGui::PushID(label);
	const bool clicked = RoundedSelectable("##nav", selected, ImVec2(0.0f, height), S(8.0f));
	ImGui::PopID();
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	if (selected)
		DrawSelectionBar(drawList, min, max, S(12.0f));
	const Theme::Fonts& fonts = Theme::GetFonts();
	ImFont* font = selected ? fonts.bold : fonts.regular;
	const float fontSize = ImGui::GetFontSize();
	const ImVec4& color = selected ? Theme::C().text : (ImGui::IsItemHovered() ? Theme::C().text : Theme::C().textDim);
	drawList->AddText(font, fontSize, ImVec2(min.x + S(18.0f), min.y + (height - fontSize) * 0.5f),
		ImGui::GetColorU32(color), label);
	if (badge > 0) {
		// Count pill on the right, e.g. active downloads.
		char count[16];
		std::snprintf(count, sizeof(count), "%d", badge);
		const float badgeFont = fontSize * (Theme::kFontSmall / Theme::kFontBody);
		const ImVec2 textSize = fonts.bold->CalcTextSizeA(badgeFont, FLT_MAX, 0.0f, count);
		const float badgeHeight = badgeFont + S(6.0f);
		const float badgeWidth = std::max(badgeHeight, textSize.x + S(12.0f));
		const ImVec2 badgeMin(max.x - S(12.0f) - badgeWidth, min.y + (height - badgeHeight) * 0.5f);
		drawList->AddRectFilled(badgeMin, ImVec2(badgeMin.x + badgeWidth, badgeMin.y + badgeHeight),
			ImGui::GetColorU32(Theme::C().accent), badgeHeight * 0.5f);
		drawList->AddText(fonts.bold, badgeFont, ImVec2(badgeMin.x + (badgeWidth - textSize.x) * 0.5f, badgeMin.y + S(3.0f)),
			ImGui::GetColorU32(Theme::C().windowBg), count);
	}
	return clicked;
}

bool NavGroupItem(const char* label, bool selected, bool expanded)
{
	const bool clicked = NavItem(label, selected);
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const ImVec2 center(max.x - S(20.0f), (min.y + max.y) * 0.5f);
	const float r = S(4.0f);
	const ImU32 color = ImGui::GetColorU32(selected || ImGui::IsItemHovered() ? Theme::C().text : Theme::C().textDim);
	if (expanded)
		ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(center.x - r, center.y - r * 0.5f),
			ImVec2(center.x + r, center.y - r * 0.5f), ImVec2(center.x, center.y + r * 0.7f), color);
	else
		ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(center.x - r * 0.5f, center.y - r),
			ImVec2(center.x + r * 0.7f, center.y), ImVec2(center.x - r * 0.5f, center.y + r), color);
	return clicked;
}

bool SubNavItem(const char* label, bool selected)
{
	const float height = S(34.0f);
	ImGui::PushID(label);
	ImGui::PushID("sub");
	const bool clicked = RoundedSelectable("##subnav", selected, ImVec2(0.0f, height), S(8.0f));
	ImGui::PopID();
	ImGui::PopID();
	const ImVec2 min = ImGui::GetItemRectMin();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float dotX = min.x + S(32.0f);
	const float midY = min.y + height * 0.5f;
	drawList->AddCircleFilled(ImVec2(dotX, midY), S(selected ? 3.5f : 2.5f),
		ImGui::GetColorU32(selected ? Theme::C().accent : Theme::C().textDim));
	const Theme::Fonts& fonts = Theme::GetFonts();
	ImFont* font = selected ? fonts.bold : fonts.regular;
	const float fontSize = ImGui::GetFontSize() * (Theme::kFontSmall + 1.0f) / Theme::kFontBody;
	const ImVec4& color = selected ? Theme::C().text : (ImGui::IsItemHovered() ? Theme::C().text : Theme::C().textDim);
	drawList->AddText(font, fontSize, ImVec2(dotX + S(12.0f), midY - fontSize * 0.5f), ImGui::GetColorU32(color), label);
	return clicked;
}

float FadeAlpha(const LoadedImage& image)
{
	if (g_reduceMotion)
		return 1.0f;
	return std::clamp(static_cast<float>((ImGui::GetTime() - image.readyTime) / 0.25), 0.0f, 1.0f);
}

void DrawPackIcon(ImDrawList* drawList, const LoadedImage* image, const Modpack& pack,
				  ImVec2 min, float size, float rounding)
{
	const ImVec2 max(min.x + size, min.y + size);
	const float alpha = image ? FadeAlpha(*image) : 0.0f;
	if (alpha < 1.0f) {
		// Placeholder tile: the pack's color with its initial.
		const ImVec4 base = PackColor(pack);
		drawList->AddRectFilled(min, max, ImGui::GetColorU32(Scaled(base, 0.55f)), rounding);
		const char initial[2] = { pack.name.empty() ? '?' : static_cast<char>(std::toupper(static_cast<unsigned char>(pack.name[0]))), '\0' };
		ImFont* font = Theme::GetFonts().bold;
		const float fontSize = size * 0.5f;
		const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, initial);
		drawList->AddText(font, fontSize, ImVec2(min.x + (size - textSize.x) * 0.5f, min.y + (size - textSize.y) * 0.5f),
			IM_COL32(255, 255, 255, 230), initial);
	}
	if (image)
		DrawImageCover(drawList, *image, min, max, rounding, alpha);
	drawList->AddRect(min, max, OutlineColor(0.08f), rounding);
}

bool PackRow(const Modpack& pack, ImageCache& images, bool selected, const std::string& meta)
{
	const float height = S(68.0f);
	const bool clicked = RoundedSelectable("##row", selected, ImVec2(0.0f, height), S(10.0f));
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	if (!ImGui::IsItemVisible())
		return clicked; // skip image requests and drawing for rows scrolled out of view

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	if (selected)
		DrawSelectionBar(drawList, min, max, S(14.0f));

	const float pad = S(10.0f);
	const float iconSize = height - pad * 2.0f;
	DrawPackIcon(drawList, images.Get(pack.iconUrl), pack, ImVec2(min.x + pad + S(4.0f), min.y + pad), iconSize, S(8.0f));

	const Theme::Fonts& fonts = Theme::GetFonts();
	const float textX = min.x + pad + S(4.0f) + iconSize + S(12.0f);
	const float nameSize = ImGui::GetFontSize();
	const float metaSize = nameSize * (Theme::kFontSmall / Theme::kFontBody);
	const float textTop = min.y + (height - (nameSize + metaSize + S(4.0f))) * 0.5f;
	DrawClippedText(drawList, fonts.bold, nameSize, ImVec2(textX, textTop), max.x - pad,
		ImGui::GetColorU32(Theme::C().text), pack.name);
	DrawClippedText(drawList, fonts.regular, metaSize, ImVec2(textX, textTop + nameSize + S(4.0f)), max.x - pad,
		ImGui::GetColorU32(Theme::C().textDim), meta);
	return clicked;
}

// Faint blocky squares, a nod to Minecraft, for banners without a gallery image.
static void DrawBlockPattern(ImDrawList* drawList, const Modpack& pack, ImVec2 min, ImVec2 max)
{
	const float cell = S(22.0f);
	std::size_t seed = std::hash<std::string>{}(pack.id);
	// Brighten very dark pack colors so the pattern stays visible.
	ImVec4 base = PackColor(pack);
	const float brightest = std::max({ base.x, base.y, base.z });
	if (brightest < 0.6f)
		base = brightest > 0.02f ? Scaled(base, 0.6f / brightest) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
	for (float y = min.y; y < max.y; y += cell) {
		for (float x = min.x; x < max.x; x += cell) {
			seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
			const unsigned roll = static_cast<unsigned>(seed >> 33) % 100;
			// Denser toward the right edge.
			const float bias = (x - min.x) / std::max(1.0f, max.x - min.x);
			if (roll > 30 + static_cast<unsigned>((1.0f - bias) * 60.0f))
				continue;
			const float shade = 0.6f + (roll % 5) * 0.12f;
			drawList->AddRectFilled(ImVec2(x + 1.0f, y + 1.0f),
				ImVec2(std::min(x + cell - 1.0f, max.x), std::min(y + cell - 1.0f, max.y)),
				WithAlpha(Scaled(base, shade), 0.10f + 0.12f * bias));
		}
	}
}

void HeroBanner(const Modpack& pack, ImageCache& images, float height, bool showGallery)
{
	const ImVec2 min = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	const ImVec2 max(min.x + width, min.y + height);
	ImGui::Dummy(ImVec2(width, height));
	if (!ImGui::IsItemVisible())
		return;

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float rounding = ImGui::GetStyle().ChildRounding;
	drawList->PushClipRect(min, max, true);

	// Background: the pack's color with a block pattern, then the gallery thumbnail,
	// then the full-size image fading in on top once it arrives.
	const ImVec4 base = PackColor(pack);
	drawList->AddRectFilled(min, max, ImGui::GetColorU32(Scaled(base, 0.35f)), rounding);
	DrawBlockPattern(drawList, pack, min, max);
	if (showGallery) {
		if (const LoadedImage* thumb = images.Get(pack.bannerUrl))
			DrawImageCover(drawList, *thumb, min, max, rounding, FadeAlpha(*thumb));
		if (const LoadedImage* full = images.Get(pack.bannerHiResUrl))
			DrawImageCover(drawList, *full, min, max, rounding, FadeAlpha(*full));
	}

	// Fade the lower part into the panel color so the text stays readable, plus a soft
	// darkening from the left for bright images. These overlays are unrounded, but they
	// are the panel color, so the parts outside the rounded corners are invisible.
	const ImVec4 panel = Theme::C().panelBg;
	const float fadeTop = min.y + height * 0.25f;
	drawList->AddRectFilledMultiColor(ImVec2(min.x, fadeTop), max,
		WithAlpha(panel, 0.0f), WithAlpha(panel, 0.0f), WithAlpha(panel, 0.97f), WithAlpha(panel, 0.97f));
	drawList->AddRectFilledMultiColor(min, ImVec2(min.x + width * 0.6f, max.y),
		WithAlpha(panel, 0.55f), WithAlpha(panel, 0.0f), WithAlpha(panel, 0.0f), WithAlpha(panel, 0.55f));

	// Foreground: icon, then name / author / chips beside it.
	const float pad = S(22.0f);
	const float iconSize = S(84.0f);
	const ImVec2 iconMin(min.x + pad, max.y - pad - iconSize);
	drawList->AddRectFilled(ImVec2(iconMin.x - S(3.0f), iconMin.y - S(3.0f)),
		ImVec2(iconMin.x + iconSize + S(3.0f), iconMin.y + iconSize + S(3.0f)), ImGui::GetColorU32(panel), S(15.0f));
	DrawPackIcon(drawList, images.Get(pack.iconUrl), pack, iconMin, iconSize, S(12.0f));

	const Theme::Fonts& fonts = Theme::GetFonts();
	const float scale = ImGui::GetFontSize() / Theme::kFontBody; // font scale incl. DPI and user scale
	const float titleSize = Theme::kFontHero * scale;
	const float bodySize = Theme::kFontBody * scale;
	const float chipSize = Theme::kFontSmall * scale;
	const float textX = iconMin.x + iconSize + S(18.0f);
	const float textRight = max.x - pad;

	const float chipHeight = chipSize + S(8.0f);
	float y = max.y - pad - chipHeight;
	float chipX = textX;
	auto addChip = [&](const std::string& text, const ImVec4& tint) {
		if (text.empty())
			return;
		const float chipWidth = ChipWidth(chipSize, text.c_str());
		if (chipX + chipWidth > textRight)
			return;
		DrawChip(drawList, ImVec2(chipX, y), chipSize, text.c_str(), tint);
		chipX += chipWidth + S(8.0f);
	};
	if (!pack.gameVersion.empty())
		addChip("Minecraft " + pack.gameVersion, Theme::C().accent);
	if (!pack.loaders.empty())
		addChip(pack.loaders, Theme::C().info);
	if (!pack.version.empty())
		addChip("v" + pack.version, Theme::C().textDim);

	if (!pack.author.empty()) {
		y -= bodySize + S(10.0f);
		DrawClippedText(drawList, fonts.regular, bodySize, ImVec2(textX, y), textRight,
			ImGui::GetColorU32(Theme::C().textDim), "by " + pack.author);
	}
	y -= titleSize + S(2.0f);
	DrawClippedText(drawList, fonts.bold, titleSize, ImVec2(textX, y), textRight,
		ImGui::GetColorU32(Theme::C().text), pack.name);

	drawList->PopClipRect();
	drawList->AddRect(min, max, OutlineColor(0.06f), rounding);
}

}

namespace Widgets {

bool EmptyState(const char* title, const char* body, const char* buttonLabel)
{
	const float width = ImGui::GetContentRegionAvail().x;
	const float wrap = std::min(width, S(460.0f));
	ImGui::Dummy(ImVec2(0.0f, S(40.0f)));

	// A small stack of blocks as the illustration.
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float block = S(26.0f);
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const ImVec2 origin(cursor.x + (width - block * 3.0f) * 0.5f, cursor.y);
	const ImVec2 cells[] = { ImVec2(0, 1), ImVec2(1, 1), ImVec2(2, 1), ImVec2(1, 0) };
	for (int i = 0; i < 4; ++i) {
		const ImVec2 cellMin(origin.x + cells[i].x * block, origin.y + cells[i].y * block);
		const ImVec4 color = Theme::Mix(Theme::C().accentDeep, Theme::C().accent, 0.25f * static_cast<float>(i));
		drawList->AddRectFilled(ImVec2(cellMin.x + S(2.0f), cellMin.y + S(2.0f)),
			ImVec2(cellMin.x + block - S(2.0f), cellMin.y + block - S(2.0f)), ImGui::GetColorU32(color), S(4.0f));
	}
	ImGui::Dummy(ImVec2(0.0f, block * 2.0f + S(14.0f)));

	auto centered = [&](ImFont* font, float size, const ImVec4& color, const char* text) {
		ImGui::PushFont(font, size);
		const float textWidth = std::min(ImGui::CalcTextSize(text, nullptr, false, wrap).x, wrap);
		const float x = ImGui::GetCursorPosX() + (width - textWidth) * 0.5f;
		ImGui::SetCursorPosX(x);
		ImGui::PushTextWrapPos(x + textWidth + 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
		ImGui::PopTextWrapPos();
		ImGui::PopFont();
	};
	const Theme::Fonts& fonts = Theme::GetFonts();
	centered(fonts.bold, Theme::kFontHeading, Theme::C().text, title);
	centered(fonts.regular, Theme::kFontBody, Theme::C().textDim, body);

	bool clicked = false;
	if (buttonLabel) {
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		const float buttonWidth = ImGui::CalcTextSize(buttonLabel, nullptr, true).x + S(48.0f);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - buttonWidth) * 0.5f);
		clicked = ImGui::Button(buttonLabel, ImVec2(buttonWidth, S(40.0f)));
	}
	return clicked;
}

bool ThemeCard(const Theme::ThemeInfo& theme, bool selected, float width)
{
	const Theme::Palette& p = theme.palette;
	const float previewHeight = width * 0.52f;
	const float fontSize = ImGui::GetFontSize();
	const float smallSize = fontSize * (Theme::kFontSmall / Theme::kFontBody);
	const float height = previewHeight + fontSize + smallSize + S(26.0f);
	ImGui::PushID(theme.id.c_str());
	const bool clicked = ImGui::InvisibleButton("##theme", ImVec2(width, height));
	const bool hovered = ImGui::IsItemHovered();
	ImGui::PopID();
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float rounding = S(10.0f);

	// Card background and outline (accent when selected).
	drawList->AddRectFilled(min, max, ImGui::GetColorU32(Theme::C().cardBg), rounding);
	const ImVec4 outline = selected ? Theme::C().accent : (hovered ? Theme::C().textDim : Theme::C().border);
	drawList->AddRect(min, max, ImGui::GetColorU32(outline), rounding, 0, selected ? S(2.5f) : S(1.0f));

	// Miniature launcher in the theme's colors: sidebar, pack list, banner, play button.
	auto col = [](const ImVec4& c) { return ImGui::GetColorU32(c); };
	const float pad = S(8.0f);
	const ImVec2 pMin(min.x + pad, min.y + pad);
	const ImVec2 pMax(max.x - pad, min.y + previewHeight);
	const float pw = pMax.x - pMin.x;
	const float ph = pMax.y - pMin.y;
	const float r = S(6.0f);
	const float g = S(4.0f);
	drawList->AddRectFilled(pMin, pMax, col(p.windowBg), r);

	const ImVec2 sideMin(pMin.x + g, pMin.y + g);
	const ImVec2 sideMax(pMin.x + pw * 0.24f, pMax.y - g);
	drawList->AddRectFilled(sideMin, sideMax, col(p.panelBg), r * 0.6f);
	const float navH = ph * 0.11f;
	for (int i = 0; i < 3; ++i) {
		const float y = sideMin.y + ph * 0.2f + i * (navH + g);
		drawList->AddRectFilled(ImVec2(sideMin.x + g, y), ImVec2(sideMax.x - g, y + navH),
			col(i == 0 ? p.accentDeep : p.frameBg), r * 0.4f);
	}

	const ImVec2 mainMin(sideMax.x + g, pMin.y + g);
	const ImVec2 mainMax(pMax.x - g, pMax.y - g);
	const float listRight = mainMin.x + (mainMax.x - mainMin.x) * 0.38f;
	drawList->AddRectFilled(mainMin, ImVec2(listRight, mainMax.y), col(p.panelBg), r * 0.6f);
	const float icon = ph * 0.13f;
	for (int i = 0; i < 4; ++i) {
		const float y = mainMin.y + g + i * (ph * 0.21f);
		drawList->AddRectFilled(ImVec2(mainMin.x + g, y), ImVec2(mainMin.x + g + icon, y + icon),
			col(Theme::Mix(p.accentDeep, p.accent, 0.25f * static_cast<float>(i))), r * 0.3f);
		const float textX = mainMin.x + g * 2.0f + icon;
		drawList->AddRectFilled(ImVec2(textX, y + icon * 0.15f), ImVec2(listRight - g, y + icon * 0.4f), col(p.text), 1.0f);
		drawList->AddRectFilled(ImVec2(textX, y + icon * 0.6f), ImVec2(listRight - g * 3.0f, y + icon * 0.8f), col(p.textDim), 1.0f);
	}

	const ImVec2 detailMin(listRight + g, mainMin.y);
	drawList->AddRectFilled(detailMin, mainMax, col(p.panelBg), r * 0.6f);
	drawList->AddRectFilledMultiColor(ImVec2(detailMin.x + g, detailMin.y + g), ImVec2(mainMax.x - g, detailMin.y + ph * 0.42f),
		col(p.accentDeep), col(p.accent), col(p.panelBg), col(p.panelBg));
	drawList->AddRectFilled(ImVec2(detailMin.x + g, detailMin.y + ph * 0.5f), ImVec2(mainMax.x - g * 4.0f, detailMin.y + ph * 0.55f),
		col(p.textDim), 1.0f);
	drawList->AddRectFilled(ImVec2(detailMin.x + g, mainMax.y - g - ph * 0.14f), ImVec2(mainMax.x - g, mainMax.y - g),
		col(p.accentDeep), r * 0.4f);

	// Name, description, and a check mark when selected.
	const Theme::Fonts& fonts = Theme::GetFonts();
	const float textY = pMax.y + S(8.0f);
	DrawClippedText(drawList, fonts.bold, fontSize, ImVec2(min.x + S(12.0f), textY), max.x - S(34.0f),
		ImGui::GetColorU32(Theme::C().text), theme.name);
	DrawClippedText(drawList, fonts.regular, smallSize, ImVec2(min.x + S(12.0f), textY + fontSize + S(2.0f)), max.x - S(10.0f),
		ImGui::GetColorU32(Theme::C().textDim), theme.description);
	if (selected) {
		const ImVec2 c(max.x - S(20.0f), textY + fontSize * 0.5f);
		const float k = S(4.5f);
		drawList->AddCircleFilled(c, S(9.0f), ImGui::GetColorU32(Theme::C().accent));
		const std::array<ImVec2, 3> check = { ImVec2(c.x - k, c.y), ImVec2(c.x - k * 0.25f, c.y + k * 0.75f), ImVec2(c.x + k, c.y - k * 0.6f) };
		drawList->AddPolyline(check.data(), 3, ImGui::GetColorU32(Theme::C().windowBg), 0, S(2.0f));
	}
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	return clicked;
}

}

namespace Widgets {

bool NewThemeCard(float width)
{
	// Same height as ThemeCard so the grid lines up.
	const float fontSize = ImGui::GetFontSize();
	const float smallSize = fontSize * (Theme::kFontSmall / Theme::kFontBody);
	const float height = width * 0.52f + fontSize + smallSize + S(26.0f);
	const bool clicked = ImGui::InvisibleButton("##new_theme", ImVec2(width, height));
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float rounding = S(10.0f);
	const ImVec4& p = hovered ? Theme::C().accent : Theme::C().textDim;
	drawList->AddRectFilled(min, max, ImGui::GetColorU32(Theme::WithAlpha(Theme::C().cardBg, hovered ? 1.0f : 0.6f)), rounding);
	// Dashed outline.
	const float dash = S(8.0f);
	const ImU32 color = ImGui::GetColorU32(Theme::WithAlpha(p, 0.8f));
	for (float x = min.x + rounding; x < max.x - rounding; x += dash * 2.0f) {
		const float end = std::min(x + dash, max.x - rounding);
		drawList->AddLine(ImVec2(x, min.y), ImVec2(end, min.y), color, S(1.5f));
		drawList->AddLine(ImVec2(x, max.y), ImVec2(end, max.y), color, S(1.5f));
	}
	for (float y = min.y + rounding; y < max.y - rounding; y += dash * 2.0f) {
		const float end = std::min(y + dash, max.y - rounding);
		drawList->AddLine(ImVec2(min.x, y), ImVec2(min.x, end), color, S(1.5f));
		drawList->AddLine(ImVec2(max.x, y), ImVec2(max.x, end), color, S(1.5f));
	}
	// Plus sign and label.
	const ImVec2 center((min.x + max.x) * 0.5f, min.y + height * 0.4f);
	const float arm = S(16.0f);
	drawList->AddLine(ImVec2(center.x - arm, center.y), ImVec2(center.x + arm, center.y), ImGui::GetColorU32(p), S(3.0f));
	drawList->AddLine(ImVec2(center.x, center.y - arm), ImVec2(center.x, center.y + arm), ImGui::GetColorU32(p), S(3.0f));
	const char* label = "Create your own";
	ImFont* font = Theme::GetFonts().bold;
	const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
	drawList->AddText(font, fontSize, ImVec2(center.x - size.x * 0.5f, center.y + arm + S(14.0f)), ImGui::GetColorU32(p), label);
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	return clicked;
}

}

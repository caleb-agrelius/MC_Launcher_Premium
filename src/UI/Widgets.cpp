#include "Widgets.h"

#include "ImageCache.h"
#include "Model/Modpack.h"
#include "Theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>

namespace Widgets {

using Theme::S;

static ImU32 WithAlpha(const ImVec4& color, float alpha)
{
	return ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
}

// The pack's Modrinth color, or the launcher accent if it has none.
static ImVec4 PackColor(const Modpack& pack)
{
	if (pack.color == 0)
		return Theme::kAccentDeep;
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
	drawList->AddText(Theme::GetFonts().bold, fontSize, ImVec2(pos.x + S(10.0f), pos.y + S(4.0f)),
		ImGui::GetColorU32(ImVec4(std::min(1.0f, tint.x * 1.25f + 0.1f), std::min(1.0f, tint.y * 1.25f + 0.1f),
			std::min(1.0f, tint.z * 1.25f + 0.1f), 1.0f)), text);
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
		ImGui::GetColorU32(Theme::kAccent), S(2.0f));
}

bool ListItem(const char* label, bool selected)
{
	ImGui::PushID(label);
	const float height = ImGui::GetTextLineHeight() + S(10.0f);
	const bool clicked = RoundedSelectable("##item", selected, ImVec2(0.0f, height), S(6.0f));
	ImGui::PopID();
	const ImVec2 min = ImGui::GetItemRectMin();
	ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + S(8.0f), min.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
		ImGui::GetColorU32(selected ? Theme::kText : (ImGui::IsItemHovered() ? Theme::kText : Theme::kTextDim)), label);
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
	Text(Theme::GetFonts().bold, Theme::kFontSmall, Theme::kTextDim, text);
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

void ThinProgressBar(float fraction)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(4.0f));
	ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-1.0f, S(8.0f)), "");
	ImGui::PopStyleVar();
}

bool NavItem(const char* label, bool selected)
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
	const ImVec4& color = selected ? Theme::kText : (ImGui::IsItemHovered() ? Theme::kText : Theme::kTextDim);
	drawList->AddText(font, fontSize, ImVec2(min.x + S(18.0f), min.y + (height - fontSize) * 0.5f),
		ImGui::GetColorU32(color), label);
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
		ImGui::GetColorU32(selected ? Theme::kAccent : Theme::kTextDim));
	const Theme::Fonts& fonts = Theme::GetFonts();
	ImFont* font = selected ? fonts.bold : fonts.regular;
	const float fontSize = ImGui::GetFontSize() * (Theme::kFontSmall + 1.0f) / Theme::kFontBody;
	const ImVec4& color = selected ? Theme::kText : (ImGui::IsItemHovered() ? Theme::kText : Theme::kTextDim);
	drawList->AddText(font, fontSize, ImVec2(dotX + S(12.0f), midY - fontSize * 0.5f), ImGui::GetColorU32(color), label);
	return clicked;
}

float FadeAlpha(const LoadedImage& image)
{
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
			WithAlpha(Theme::kText, 0.9f), initial);
	}
	if (image)
		DrawImageCover(drawList, *image, min, max, rounding, alpha);
	drawList->AddRect(min, max, WithAlpha(ImVec4(1, 1, 1, 1), 0.08f), rounding);
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
		ImGui::GetColorU32(Theme::kText), pack.name);
	DrawClippedText(drawList, fonts.regular, metaSize, ImVec2(textX, textTop + nameSize + S(4.0f)), max.x - pad,
		ImGui::GetColorU32(Theme::kTextDim), meta);
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

void HeroBanner(const Modpack& pack, ImageCache& images, float height)
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
	if (const LoadedImage* thumb = images.Get(pack.bannerUrl))
		DrawImageCover(drawList, *thumb, min, max, rounding, FadeAlpha(*thumb));
	if (const LoadedImage* full = images.Get(pack.bannerHiResUrl))
		DrawImageCover(drawList, *full, min, max, rounding, FadeAlpha(*full));

	// Fade the lower part into the panel color so the text stays readable, plus a soft
	// darkening from the left for bright images. These overlays are unrounded, but they
	// are the panel color, so the parts outside the rounded corners are invisible.
	const ImVec4 panel = Theme::kPanelBg;
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
		addChip("Minecraft " + pack.gameVersion, Theme::kAccent);
	if (!pack.loaders.empty())
		addChip(pack.loaders, ImVec4(0.45f, 0.65f, 0.95f, 1.0f));
	if (!pack.version.empty())
		addChip("v" + pack.version, ImVec4(0.75f, 0.75f, 0.80f, 1.0f));

	if (!pack.author.empty()) {
		y -= bodySize + S(10.0f);
		DrawClippedText(drawList, fonts.regular, bodySize, ImVec2(textX, y), textRight,
			ImGui::GetColorU32(Theme::kTextDim), "by " + pack.author);
	}
	y -= titleSize + S(2.0f);
	DrawClippedText(drawList, fonts.bold, titleSize, ImVec2(textX, y), textRight,
		ImGui::GetColorU32(Theme::kText), pack.name);

	drawList->PopClipRect();
	drawList->AddRect(min, max, WithAlpha(ImVec4(1, 1, 1, 1), 0.06f), rounding);
}

}

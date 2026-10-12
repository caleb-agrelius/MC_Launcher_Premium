#include "Widgets.h"

#include "ImageCache.h"
#include "Logo.h"
#include "Model/Modpack.h"
#include "Theme.h"
#include "Util/Text.h"

#include "imgui_internal.h" // DC.CursorPos, for entrance animations

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace Widgets {

using Theme::S;

static bool g_reduceMotion = false;

void SetReduceMotion(bool reduce)
{
	g_reduceMotion = reduce;
}

static float g_animationSpeed = 1.0f;

void SetAnimationSpeed(float speed)
{
	g_animationSpeed = std::clamp(speed, 0.05f, 10.0f);
}

float AnimationSpeed()
{
	return g_animationSpeed;
}

static constexpr double kEntranceSeconds = 0.32;
static constexpr double kEntranceStagger = 0.045; // per list item
static constexpr int kEntranceMaxStagger = 10;    // later items don't wait any longer
static std::vector<float> g_entranceOffsets;      // cursor lift per open BeginEntrance

void BeginEntrance(double start, int index)
{
	float progress = 1.0f;
	if (!g_reduceMotion) {
		const double speed = g_animationSpeed;
		const double delay = kEntranceStagger * std::min(index, kEntranceMaxStagger) / speed;
		const double t = std::clamp((ImGui::GetTime() - start - delay) * speed / kEntranceSeconds, 0.0, 1.0);
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

// Windows that called SmoothScroll recently, with the last frame they did.
static std::unordered_map<ImGuiID, int> g_smoothWindows;
static int g_wheelTargetFrame = -1;
static ImGuiID g_wheelTarget = 0;

// The window the wheel scrolls this frame: starting at the window under the mouse, the
// first one that can scroll and takes the wheel, walking up through parent child windows.
// Cards (no wheel, no SmoothScroll) pass it on to the page around them. The walk stops at a
// popup or top-level window, so a combo list under the mouse never scrolls the page behind it.
static ImGuiID WheelTarget()
{
	ImGuiContext& g = *GImGui;
	if (g_wheelTargetFrame == g.FrameCount)
		return g_wheelTarget;
	g_wheelTargetFrame = g.FrameCount;
	g_wheelTarget = 0;
	for (ImGuiWindow* window = g.HoveredWindow; window; window = window->ParentWindow) {
		const auto smooth = g_smoothWindows.find(window->ID);
		const bool usesSmooth = smooth != g_smoothWindows.end() && smooth->second >= g.FrameCount - 2;
		const bool takesWheel = usesSmooth || !(window->Flags & ImGuiWindowFlags_NoScrollWithMouse);
		if (window->ScrollMax.y > 0.0f && takesWheel && !(window->Flags & ImGuiWindowFlags_NoMouseInputs)) {
			g_wheelTarget = window->ID;
			break;
		}
		if (!(window->Flags & ImGuiWindowFlags_ChildWindow) || (window->Flags & ImGuiWindowFlags_Popup))
			break;
	}
	return g_wheelTarget;
}

void SmoothScroll()
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	const int frame = ImGui::GetFrameCount();
	g_smoothWindows[window->ID] = frame;
	ImGuiStorage* storage = ImGui::GetStateStorage();
	const float scroll = ImGui::GetScrollY();
	const float maxScroll = ImGui::GetScrollMaxY();
	float* target = storage->GetFloatRef(ImGui::GetID("##smooth_target"), scroll);
	float* last = storage->GetFloatRef(ImGui::GetID("##smooth_last"), scroll);
	// Scrollbar drags, keyboard navigation and SetScrollHereY move the view on their own.
	if (std::fabs(scroll - *last) > 0.5f)
		*target = scroll;

	const ImGuiIO& io = ImGui::GetIO();
	// Shift turns the wheel sideways, like ImGui's own scrolling.
	float wheelX = io.MouseWheelH;
	float wheelY = io.MouseWheel;
	if (io.KeyShift && wheelX == 0.0f) {
		wheelX = wheelY;
		wheelY = 0.0f;
	}
	if (wheelY != 0.0f && !io.KeyCtrl && WheelTarget() == window->ID)
		*target -= wheelY * S(120.0f);
	if (wheelX != 0.0f && !io.KeyCtrl && ImGui::GetScrollMaxX() > 0.0f &&
		ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_NoPopupHierarchy))
		ImGui::SetScrollX(std::clamp(ImGui::GetScrollX() - wheelX * S(120.0f), 0.0f, ImGui::GetScrollMaxX()));
	*target = std::clamp(*target, 0.0f, maxScroll);

	float next = *target;
	if (!g_reduceMotion) {
		const float follow = 1.0f - std::exp(-io.DeltaTime * 14.0f * g_animationSpeed); // ~70 ms time constant at normal speed
		next = scroll + (*target - scroll) * follow;
		if (std::fabs(*target - next) < 0.5f)
			next = *target;
		// ImGui rounds the scroll position to whole pixels. Round here too, and always move at
		// least one pixel, or the last pixels stall and read as a scrollbar drag above.
		next = std::round(next);
		if (next == scroll && *target != scroll)
			next = scroll + (*target > scroll ? 1.0f : -1.0f);
	}
	if (next != scroll)
		ImGui::SetScrollY(next);
	*last = next;
}

bool BeginScrollArea(const char* id, ImVec2 size, ImGuiChildFlags childFlags, ImGuiWindowFlags windowFlags)
{
	const bool visible = ImGui::BeginChild(id, size, childFlags, windowFlags | ImGuiWindowFlags_NoScrollWithMouse);
	// Keep content off the scrollbar: rows and cards end a little before it rather than
	// touching it.
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->ScrollbarY) {
		const float gap = S(6.0f);
		window->WorkRect.Max.x -= gap;
		window->ContentRegionRect.Max.x -= gap;
	}
	return visible;
}

// Content scrolled out of the top fades into the panel color instead of being cut off. Cards are
// child windows drawn after their parent, so the fade is its own click-through child, begun last.
static void DrawTopFade()
{
	const float scroll = ImGui::GetScrollY();
	if (scroll < 1.0f)
		return;
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	// Inside a bordered panel's outline and rounded corners, so the fade never paints over them. A borderless
	// area has neither, and the fade runs to its very edge so content there fades too.
	const float border = window->WindowBorderSize;
	const float inset = border > 0.0f ? border + std::ceil(window->WindowRounding * 0.5f) : 0.0f;
	const ImVec2 min(std::max(window->InnerRect.Min.x, window->Pos.x + inset), std::max(window->InnerRect.Min.y, window->Pos.y + border));
	const float width = std::min(window->InnerRect.Max.x, window->Pos.x + window->Size.x - inset) - min.x;
	const float height = std::min(S(64.0f), window->InnerRect.GetHeight() * 0.3f);
	if (height < 1.0f || width < 1.0f)
		return;
	ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
	color.w = 1.0f;
	// Eases in over the first few pixels so the fade doesn't pop in at the first notch. Fully opaque at the
	// edge, so no line of cut-off content shows.
	const float strength = std::clamp(scroll / S(24.0f), 0.0f, 1.0f);
	ImGui::SetCursorScreenPos(min);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
	if (ImGui::BeginChild("##top_fade", ImVec2(width, height), ImGuiChildFlags_None,
			ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
		// Two linear steps approximate an ease-out: dense at the top, then a long soft tail with no visible end.
		const float midY = min.y + height * 0.4f;
		const ImU32 solid = ImGui::GetColorU32(Theme::WithAlpha(color, strength));
		const ImU32 mid = ImGui::GetColorU32(Theme::WithAlpha(color, strength * 0.5f));
		const ImU32 clear = ImGui::GetColorU32(Theme::WithAlpha(color, 0.0f));
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilledMultiColor(min, ImVec2(min.x + width, midY), solid, solid, mid, mid);
		drawList->AddRectFilledMultiColor(ImVec2(min.x, midY), ImVec2(min.x + width, min.y + height), mid, mid, clear, clear);
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

void EndScrollArea()
{
	SmoothScroll();
	DrawTopFade();
	ImGui::EndChild();
}

bool CloseX(const char* id, ImVec2 min, float size)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	const ImRect rect(min, ImVec2(min.x + size, min.y + size));
	const ImGuiID key = window->GetID(id);
	ImGui::KeepAliveID(key); // not an ImGui item, so nothing else keeps the click alive between frames
	bool hovered = false;
	bool held = false;
	const bool pressed = ImGui::ButtonBehavior(rect, key, &hovered, &held);
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	ImDrawList* drawList = window->DrawList;
	if (hovered || held)
		drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), S(6.0f));
	const ImVec2 center((rect.Min.x + rect.Max.x) * 0.5f, (rect.Min.y + rect.Max.y) * 0.5f);
	const float arm = size * 0.22f;
	const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
	drawList->AddLine(ImVec2(center.x - arm, center.y - arm), ImVec2(center.x + arm, center.y + arm), color, S(1.5f));
	drawList->AddLine(ImVec2(center.x - arm, center.y + arm), ImVec2(center.x + arm, center.y - arm), color, S(1.5f));
	return pressed;
}

bool ModalCloseButton()
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	const ImRect bar = window->TitleBarRect();
	const float size = bar.GetHeight() - S(8.0f);
	ImGui::PushClipRect(bar.Min, bar.Max, false); // the window's own clip rect too, or hovering the title bar is clipped away
	const bool clicked = CloseX("##modal_close", ImVec2(bar.Max.x - size - S(8.0f), bar.Min.y + S(4.0f)), size);
	ImGui::PopClipRect();
	return clicked;
}

bool PaneSplitter(const char* id, float* width, float minWidth, float maxWidth)
{
	const float gap = ImGui::GetStyle().ItemSpacing.x;
	const float height = ImGui::GetContentRegionAvail().y;
	ImGui::InvisibleButton(id, ImVec2(gap, std::max(1.0f, height)));
	const bool hovered = ImGui::IsItemHovered();
	const bool active = ImGui::IsItemActive();
	if (hovered || active)
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	bool changed = false;
	if (active) {
		const float delta = ImGui::GetIO().MouseDelta.x / S(1.0f);
		const float next = std::clamp(*width + delta, minWidth, std::max(minWidth, maxWidth));
		if (next != *width) {
			*width = next;
			changed = true;
		}
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !active)
		ImGui::SetTooltip("Drag to resize");
	// A soft grip in the middle of the gap, shown while hovered or dragged.
	if (hovered || active) {
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const float x = (min.x + max.x) * 0.5f;
		const float y = (min.y + max.y) * 0.5f;
		const float half = std::min(S(28.0f), (max.y - min.y) * 0.5f);
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x - S(2.0f), y - half), ImVec2(x + S(2.0f), y + half),
			ImGui::GetColorU32(active ? Theme::C().accent : Theme::C().textDim), S(2.0f));
	}
	return changed;
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

bool ListItem(const char* label, bool selected, bool closePopup, const char* hint, const ImVec4* textColor)
{
	const char* labelEnd = ImGui::FindRenderedTextEnd(label);
	const float hintWidth = hint && *hint ? ImGui::CalcTextSize(hint).x + S(16.0f) : 0.0f;
	float width = 0.0f; // the available width
	if (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_Popup)
		width = std::max(ImGui::GetContentRegionAvail().x, ImGui::CalcTextSize(label, labelEnd).x + hintWidth + S(16.0f));
	ImGui::PushID(label);
	const float height = ImGui::GetTextLineHeight() + S(10.0f);
	const bool clicked = RoundedSelectable("##item", selected, ImVec2(width, height), S(6.0f));
	ImGui::PopID();
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const float textY = min.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	if (hintWidth > 0.0f)
		drawList->AddText(ImVec2(max.x - S(8.0f) - (hintWidth - S(16.0f)), textY), ImGui::GetColorU32(Theme::C().textDim), hint);
	// Long labels end in "..." instead of running past the row.
	DrawClippedText(drawList, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + S(8.0f), textY),
		max.x - S(8.0f) - hintWidth,
		textColor ? ImGui::GetColorU32(*textColor)
			: ImGui::GetColorU32(selected ? Theme::C().text : (ImGui::IsItemHovered() ? Theme::C().text : Theme::C().textDim)),
		std::string(label, labelEnd));
	if (selected && ImGui::IsWindowAppearing())
		ImGui::SetScrollHereY();
	if (clicked && closePopup)
		ImGui::CloseCurrentPopup(); // like ImGui::Selectable in a combo; does nothing outside a popup
	return clicked;
}

bool MenuItem(const char* label, const char* shortcut, bool enabled)
{
	ImGui::BeginDisabled(!enabled);
	const bool clicked = ListItem(label, false, true, shortcut);
	ImGui::EndDisabled();
	return clicked && enabled;
}

bool BeginSelect(const char* id, const char* preview, ImGuiComboFlags flags)
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float width = ImGui::CalcItemWidth();
	const float height = ImGui::GetFrameHeight();
	const bool open = ImGui::BeginCombo(id, "", flags | ImGuiComboFlags_NoArrowButton);
	const bool hovered = !open && ImGui::IsItemHovered();
	// The choice and a chevron, drawn over the frame BeginCombo just drew.
	const ImVec2 padding = ImGui::GetStyle().FramePadding;
	const float r = S(4.0f);
	const ImVec2 center(pos.x + width - padding.x - r, pos.y + height * 0.5f);
	DrawClippedText(drawList, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + padding.x, pos.y + padding.y),
		center.x - r - S(8.0f), ImGui::GetColorU32(ImGuiCol_Text), preview ? preview : "");
	const ImU32 chevron = ImGui::GetColorU32(open ? Theme::C().accent : hovered ? Theme::C().text : Theme::C().textDim);
	if (open)
		drawList->AddTriangleFilled(ImVec2(center.x - r, center.y + r * 0.5f), ImVec2(center.x + r, center.y + r * 0.5f),
			ImVec2(center.x, center.y - r * 0.7f), chevron);
	else
		drawList->AddTriangleFilled(ImVec2(center.x - r, center.y - r * 0.5f), ImVec2(center.x + r, center.y - r * 0.5f),
			ImVec2(center.x, center.y + r * 0.7f), chevron);
	return open;
}

void EndSelect()
{
	ImGui::EndCombo();
}

bool Select(const char* id, int* current, const char* const* labels, int count)
{
	const bool valid = *current >= 0 && *current < count;
	bool changed = false;
	if (BeginSelect(id, valid ? labels[*current] : "")) {
		for (int i = 0; i < count; ++i) {
			ImGui::PushID(i);
			if (ListItem(labels[i], i == *current) && i != *current) {
				*current = i;
				changed = true;
			}
			ImGui::PopID();
		}
		EndSelect();
	}
	return changed;
}

bool PrimaryButton(const char* label, ImVec2 size)
{
	const Theme::Palette& p = Theme::C();
	ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Mix(p.accent, p.text, 0.15f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Mix(p.accent, p.windowBg, 0.15f));
	ImGui::PushStyleColor(ImGuiCol_Text, p.windowBg);
	const bool clicked = ImGui::Button(label, size);
	ImGui::PopStyleColor(4);
	return clicked;
}

bool DangerButton(const char* label, ImVec2 size)
{
	const Theme::Palette& p = Theme::C();
	ImGui::PushStyleColor(ImGuiCol_Button, Theme::Mix(p.cardBg, p.error, 0.55f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Mix(p.cardBg, p.error, 0.75f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Mix(p.cardBg, p.error, 0.9f));
	const bool clicked = ImGui::Button(label, size);
	ImGui::PopStyleColor(3);
	return clicked;
}

float ButtonWidth(const char* label)
{
	return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
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

static void DrawSpinnerAt(ImDrawList* drawList, ImVec2 pos, float size, const ImVec4& color)
{
	const float thickness = std::max(1.5f, size * 0.14f);
	const float radius = size * 0.5f - thickness;
	const ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
	const float start = static_cast<float>(ImGui::GetTime()) * 6.0f * g_animationSpeed;
	drawList->PathArcTo(center, radius, start, start + 3.14159265f * 1.4f, 24);
	drawList->PathStroke(ImGui::GetColorU32(color), 0, thickness);
}

void Spinner(const ImVec4& color)
{
	const float size = ImGui::GetTextLineHeight();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(size, size));
	DrawSpinnerAt(ImGui::GetWindowDrawList(), pos, size, color);
}

void Skeleton(ImVec2 size, float rounding, float barHeight)
{
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::Dummy(size);
	float pulse = 0.5f;
	if (!g_reduceMotion)
		pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f * g_animationSpeed);
	const ImVec4 color = Theme::WithAlpha(Theme::Mix(Theme::C().cardBg, Theme::C().textDim, 0.35f), 0.35f + 0.35f * pulse);
	const float bar = barHeight > 0.0f ? std::min(barHeight, size.y) : size.y;
	const float top = pos.y + (size.y - bar) * 0.5f;
	ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, top), ImVec2(pos.x + size.x, top + bar), ImGui::GetColorU32(color), S(rounding));
}

void PadToHeight(float startCursorY, float minHeight)
{
	const float missing = startCursorY + minHeight - ImGui::GetCursorPosY();
	const float spacing = ImGui::GetStyle().ItemSpacing.y;
	if (missing > spacing)
		ImGui::Dummy(ImVec2(0.0f, missing - spacing));
}

void LoadingField(const char* label, float width)
{
	const float fieldWidth = width > 0.0f ? width : ImGui::CalcItemWidth();
	const float height = ImGui::GetFrameHeight();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(fieldWidth, height));
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(pos, ImVec2(pos.x + fieldWidth, pos.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg),
		ImGui::GetStyle().FrameRounding);
	const float spinner = ImGui::GetTextLineHeight();
	const float padding = ImGui::GetStyle().FramePadding.x;
	DrawSpinnerAt(drawList, ImVec2(pos.x + padding, pos.y + (height - spinner) * 0.5f), spinner, Theme::C().accent);
	DrawClippedText(drawList, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + padding * 2.0f + spinner, pos.y + ImGui::GetStyle().FramePadding.y),
		pos.x + fieldWidth - padding, ImGui::GetColorU32(Theme::C().textDim), label);
}

void Chip(const char* text, const ImVec4& tint)
{
	const float fontSize = ImGui::GetFontSize() * (Theme::kFontSmall / Theme::kFontBody);
	const float height = fontSize + S(8.0f);
	// Centered on the line it shares: next to buttons (or after AlignTextToFramePadding) the
	// line is a frame tall, and the chip sits in its middle rather than at its top.
	// After plain text the line is shorter than the chip; the chip still centers on it, spilling
	// a little into the spacing above and below rather than pushing the next line down.
	ImVec2 pos = ImGui::GetCursorScreenPos();
	const float lineHeight = ImGui::GetCurrentWindow()->DC.CurrLineSize.y;
	if (lineHeight > 0.0f)
		pos.y += std::floor((lineHeight - height) * 0.5f);
	DrawChip(ImGui::GetWindowDrawList(), pos, fontSize, text, tint);
	ImGui::Dummy(ImVec2(ChipWidth(fontSize, text), lineHeight > 0.0f ? std::min(lineHeight, height) : height));
}

ImVec4 ModifiedColor()
{
	// Amber reads as "changed" on dark and light themes alike, apart from accent and error.
	return Theme::C().light ? ImVec4(0.72f, 0.45f, 0.05f, 1.0f) : ImVec4(0.95f, 0.70f, 0.28f, 1.0f);
}

void ModifiedChip(const std::string& changes)
{
	Chip("Modified", ModifiedColor());
	if (!changes.empty())
		ImGui::SetItemTooltip("You changed this pack: %s", changes.c_str());
}

bool ToggleRow(const char* label, bool* value)
{
	const bool changed = Toggle(label, value);
	const char* end = ImGui::FindRenderedTextEnd(label);
	if (end != label) {
		ImGui::SameLine(0.0f, S(10.0f));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label, end);
		if (ImGui::IsItemClicked()) {
			*value = !*value;
			return true;
		}
	}
	return changed;
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

float FadeAlpha(const LoadedImage& image)
{
	if (g_reduceMotion)
		return 1.0f;
	return std::clamp(static_cast<float>((ImGui::GetTime() - image.readyTime) * g_animationSpeed / 0.25), 0.0f, 1.0f);
}

void DrawPackIcon(ImDrawList* drawList, const LoadedImage* image, const Modpack& pack,
				  ImVec2 min, float size, float rounding, ImDrawFlags flags)
{
	const ImVec2 max(min.x + size, min.y + size);
	const float alpha = image ? FadeAlpha(*image) : 0.0f;
	if (alpha < 1.0f) {
		// Placeholder tile: the pack's color with its initial.
		const ImVec4 base = PackColor(pack);
		drawList->AddRectFilled(min, max, ImGui::GetColorU32(Scaled(base, 0.55f)), rounding, flags);
		const char initial[2] = { pack.name.empty() ? '?' : static_cast<char>(std::toupper(static_cast<unsigned char>(pack.name[0]))), '\0' };
		ImFont* font = Theme::GetFonts().bold;
		const float fontSize = size * 0.5f;
		const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, initial);
		drawList->AddText(font, fontSize, ImVec2(min.x + (size - textSize.x) * 0.5f, min.y + (size - textSize.y) * 0.5f),
			IM_COL32(255, 255, 255, 230), initial);
	}
	if (image)
		DrawImageCover(drawList, *image, min, max, rounding, alpha, flags);
	drawList->AddRect(min, max, OutlineColor(0.08f), rounding, 1.0f, flags);
}

// A small pill with the source's name in its color, one text line (lineHeight) tall, drawn at pos.
// Returns its width. Shared by the pack list rows and the grid cards.
static float DrawSourceTag(ImDrawList* drawList, const char* tag, const ImVec4& color, ImVec2 pos, float lineHeight)
{
	const float textSize = lineHeight * 0.92f;
	ImFont* bold = Theme::GetFonts().bold;
	const float width = bold->CalcTextSizeA(textSize, FLT_MAX, 0.0f, tag).x + S(10.0f);
	drawList->AddRectFilled(ImVec2(pos.x, pos.y - S(1.0f)), ImVec2(pos.x + width, pos.y + lineHeight + S(1.0f)),
		ImGui::GetColorU32(Theme::WithAlpha(color, 0.22f)), S(4.0f));
	drawList->AddText(bold, textSize, ImVec2(pos.x + S(5.0f), pos.y + (lineHeight - textSize) * 0.5f),
		ImGui::GetColorU32(Theme::Mix(color, Theme::C().text, Theme::C().light ? 0.0f : 0.35f)), tag);
	return width;
}

bool PackRow(const Modpack& pack, ImageCache& images, bool selected, const std::string& meta,
	const char* tag, const ImVec4& tagColor)
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
	float metaX = textX;
	const float metaY = textTop + nameSize + S(4.0f);
	if (tag && tag[0] != '\0')
		metaX += DrawSourceTag(drawList, tag, tagColor, ImVec2(metaX, metaY), metaSize) + S(6.0f); // e.g. "CurseForge"
	DrawClippedText(drawList, fonts.regular, metaSize, ImVec2(metaX, metaY), max.x - pad,
		ImGui::GetColorU32(Theme::C().textDim), meta);
	return clicked;
}

// A small pill of text with a border, for the category chips on a grid card. Returns its width.
static float DrawOutlineChip(ImDrawList* drawList, ImVec2 pos, float height, float maxWidth, const std::string& text, float size)
{
	ImFont* font = Theme::GetFonts().regular;
	const float padX = S(8.0f);
	const float width = std::min(maxWidth, font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x + padX * 2.0f + 1.0f);
	drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(Theme::C().frameBg), S(5.0f));
	drawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(Theme::C().border), S(5.0f));
	DrawClippedText(drawList, font, size, ImVec2(pos.x + padX, pos.y + (height - size) * 0.5f), pos.x + width - padX,
		ImGui::GetColorU32(Theme::C().text), text);
	return width;
}

// The category names a grid card shows: the pack's categories, else its loaders, else its Minecraft version.
static std::vector<std::string> CardTags(const Modpack& pack)
{
	if (!pack.categories.empty())
		return pack.categories;
	std::vector<std::string> tags;
	std::size_t start = 0;
	while (start < pack.loaders.size()) {
		std::size_t end = pack.loaders.find(", ", start);
		if (end == std::string::npos)
			end = pack.loaders.size();
		if (end > start)
			tags.push_back(CategoryLabel(pack.loaders.substr(start, end - start)));
		start = end + 2;
	}
	if (tags.empty() && !pack.gameVersion.empty())
		tags.push_back(pack.gameVersion);
	return tags;
}

// A down arrow onto a line: the "downloads" glyph, drawn so it doesn't depend on the font.
static void DrawDownloadGlyph(ImDrawList* drawList, ImVec2 min, float size, ImU32 color)
{
	const float x = min.x + size * 0.5f;
	const float thick = std::max(1.0f, size * 0.13f);
	drawList->AddLine(ImVec2(x, min.y + size * 0.1f), ImVec2(x, min.y + size * 0.65f), color, thick);
	drawList->AddLine(ImVec2(x - size * 0.28f, min.y + size * 0.4f), ImVec2(x, min.y + size * 0.68f), color, thick);
	drawList->AddLine(ImVec2(x + size * 0.28f, min.y + size * 0.4f), ImVec2(x, min.y + size * 0.68f), color, thick);
	drawList->AddLine(ImVec2(min.x + size * 0.15f, min.y + size * 0.92f), ImVec2(min.x + size * 0.85f, min.y + size * 0.92f), color, thick);
}

// A clock face: the "last updated" glyph.
static void DrawClockGlyph(ImDrawList* drawList, ImVec2 min, float size, ImU32 color, ImU32 hands)
{
	const ImVec2 center(min.x + size * 0.5f, min.y + size * 0.5f);
	const float thick = std::max(1.0f, size * 0.12f);
	drawList->AddCircleFilled(center, size * 0.5f, color, 24);
	drawList->AddLine(center, ImVec2(center.x, center.y - size * 0.3f), hands, thick);
	drawList->AddLine(center, ImVec2(center.x + size * 0.22f, center.y + size * 0.1f), hands, thick);
}

bool PackCard(const Modpack& pack, ImageCache& images, bool selected, const std::string& meta,
	const char* tag, const ImVec4& tagColor, float width)
{
	const Theme::Palette& p = Theme::C();
	const Theme::Fonts& fonts = Theme::GetFonts();
	const float pad = S(10.0f);
	const float rounding = S(8.0f);
	const float nameSize = ImGui::GetFontSize();
	const float metaSize = nameSize * (Theme::kFontSmall / Theme::kFontBody);
	const float chipHeight = metaSize + S(8.0f);
	const float coverHeight = width * 0.6f;
	const float iconSize = S(44.0f);
	const float height = coverHeight + pad + iconSize + S(8.0f) + chipHeight + S(10.0f) + S(1.0f) + S(8.0f) +
		metaSize + pad;
	ImGui::InvisibleButton("##card", ImVec2(std::max(1.0f, width), height));
	const bool clicked = ImGui::IsItemClicked();
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	if (!ImGui::IsItemVisible())
		return clicked; // skip image requests and drawing for cards scrolled out of view

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? Theme::Mix(p.cardBg, p.accent, 0.08f) : p.cardBg), rounding);
	// The cover: the pack's banner image when it has one, else a tile in the pack's color; the banner fades in.
	const ImVec2 coverMax(max.x, min.y + coverHeight);
	drawList->AddRectFilled(min, coverMax, ImGui::GetColorU32(Theme::Mix(p.cardBg, PackColor(pack), 0.3f)), rounding,
		ImDrawFlags_RoundCornersTop);
	if (const LoadedImage* banner = pack.bannerUrl.empty() ? nullptr : images.Get(pack.bannerUrl))
		DrawImageCover(drawList, *banner, min, coverMax, rounding, FadeAlpha(*banner), ImDrawFlags_RoundCornersTop);
	if (tag && tag[0] != '\0') {
		// The source, on a dark pill over the cover with a dot in its color.
		const float tagSize = metaSize;
		const float dot = S(4.0f);
		const ImVec2 tagMin(min.x + S(8.0f), min.y + S(8.0f));
		const float tagWidth = fonts.bold->CalcTextSizeA(tagSize, FLT_MAX, 0.0f, tag).x + S(10.0f) + dot * 2.0f + S(5.0f);
		drawList->AddRectFilled(tagMin, ImVec2(tagMin.x + tagWidth, tagMin.y + tagSize + S(8.0f)), IM_COL32(12, 14, 18, 215), S(4.0f));
		drawList->AddCircleFilled(ImVec2(tagMin.x + S(7.0f) + dot, tagMin.y + (tagSize + S(8.0f)) * 0.5f), dot, ImGui::GetColorU32(tagColor));
		drawList->AddText(fonts.bold, tagSize, ImVec2(tagMin.x + S(12.0f) + dot * 2.0f, tagMin.y + S(4.0f)),
			IM_COL32(240, 242, 245, 255), tag);
	}

	const float left = min.x + pad;
	const float right = max.x - pad;
	float y = min.y + coverHeight + pad;
	// The pack's icon at the left, with the name and author beside it, centered on the icon.
	DrawPackIcon(drawList, images.Get(pack.iconUrl), pack, ImVec2(left, y), iconSize, S(8.0f));
	const float textLeft = left + iconSize + S(10.0f);
	const float textTop = y + (iconSize - (nameSize + S(3.0f) + metaSize)) * 0.5f;
	DrawClippedText(drawList, fonts.bold, nameSize, ImVec2(textLeft, textTop), right, ImGui::GetColorU32(p.text), pack.name);
	DrawClippedText(drawList, fonts.regular, metaSize, ImVec2(textLeft, textTop + nameSize + S(3.0f)), right, ImGui::GetColorU32(p.textDim),
		pack.author.empty() ? std::string() : "by " + pack.author);
	y += iconSize + S(8.0f);

	// The first category, and how many more there are.
	const std::vector<std::string> tags = CardTags(pack);
	if (!tags.empty()) {
		const std::string more = tags.size() > 1 ? "+" + std::to_string(tags.size() - 1) : std::string();
		const float moreWidth = more.empty() ? 0.0f
			: fonts.regular->CalcTextSizeA(metaSize, FLT_MAX, 0.0f, more.c_str()).x + S(16.0f) + S(6.0f);
		const float used = DrawOutlineChip(drawList, ImVec2(left, y), chipHeight, right - left - moreWidth, tags.front(), metaSize);
		if (!more.empty())
			DrawOutlineChip(drawList, ImVec2(left + used + S(6.0f), y), chipHeight, S(60.0f), more, metaSize);
	}
	y += chipHeight + S(10.0f);

	// A rule, then the stats: downloads and the last update.
	drawList->AddLine(ImVec2(left, y), ImVec2(right, y), ImGui::GetColorU32(p.border));
	y += S(1.0f) + S(8.0f);
	const ImU32 dim = ImGui::GetColorU32(p.textDim);
	const std::string date = FormatShortDate(pack.updated);
	if (pack.downloads == 0 && date.empty()) {
		DrawClippedText(drawList, fonts.regular, metaSize, ImVec2(left, y), right, dim, meta);
	} else {
		float x = left;
		if (pack.downloads > 0) {
			DrawDownloadGlyph(drawList, ImVec2(x, y), metaSize, dim);
			x += metaSize + S(5.0f);
			const std::string count = FormatCount(pack.downloads);
			DrawClippedText(drawList, fonts.regular, metaSize, ImVec2(x, y), right, dim, count);
			x += fonts.regular->CalcTextSizeA(metaSize, FLT_MAX, 0.0f, count.c_str()).x + S(12.0f);
		}
		if (!date.empty() && x + metaSize < right) {
			DrawClockGlyph(drawList, ImVec2(x, y), metaSize, dim, ImGui::GetColorU32(p.cardBg));
			DrawClippedText(drawList, fonts.regular, metaSize, ImVec2(x + metaSize + S(5.0f), y), right, dim, date);
		}
	}
	drawList->AddRect(min, max, ImGui::GetColorU32(selected ? p.accent : hovered ? Theme::Mix(p.border, p.accent, 0.5f) : p.border),
		rounding, selected ? S(2.0f) : 1.0f);
	return clicked;
}

bool ToggleChip(const char* label, bool on, const ImVec4& color, bool enabled)
{
	const Theme::Palette& p = Theme::C();
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(14.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12.0f), S(5.0f)));
	ImGui::PushStyleColor(ImGuiCol_Button, on ? Theme::Mix(p.cardBg, color, 0.32f) : p.frameBg);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Mix(p.cardBg, color, on ? 0.45f : 0.2f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Mix(p.cardBg, color, 0.55f));
	ImGui::PushStyleColor(ImGuiCol_Text, on ? p.text : p.textDim);
	ImGui::BeginDisabled(!enabled);
	// A colored dot before the label.
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const std::string padded = std::string("    ") + label;
	const bool clicked = ImGui::Button(padded.c_str());
	const float radius = S(4.0f);
	ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + S(14.0f), pos.y + ImGui::GetItemRectSize().y * 0.5f), radius,
		ImGui::GetColorU32(on ? color : Theme::WithAlpha(color, 0.45f)));
	ImGui::EndDisabled();
	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar(2);
	return clicked;
}

bool ViewModeButton(const char* id, bool grid, bool on)
{
	const Theme::Palette& p = Theme::C();
	const float size = ImGui::GetFrameHeight();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
	const bool hovered = ImGui::IsItemHovered();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 end(pos.x + size, pos.y + size);
	draw->AddRectFilled(pos, end, ImGui::GetColorU32(on ? Theme::Mix(p.cardBg, p.accent, 0.32f) : hovered ? Theme::Mix(p.cardBg, p.accent, 0.2f) : p.frameBg), S(8.0f));
	const ImU32 ink = ImGui::GetColorU32(on ? p.text : p.textDim);
	const float pad = size * 0.28f;
	const float inner = size - 2.0f * pad;
	if (grid) {
		const float gap = S(2.0f);
		const float cell = (inner - gap) * 0.5f;
		for (int row = 0; row < 2; ++row) {
			for (int col = 0; col < 2; ++col) {
				const ImVec2 min(pos.x + pad + col * (cell + gap), pos.y + pad + row * (cell + gap));
				draw->AddRectFilled(min, ImVec2(min.x + cell, min.y + cell), ink, S(1.5f));
			}
		}
	} else {
		const float thick = S(2.0f);
		for (int line = 0; line < 3; ++line) {
			const float y = pos.y + pad + line * (inner - thick) * 0.5f;
			draw->AddRectFilled(ImVec2(pos.x + pad, y), ImVec2(pos.x + pad + inner, y + thick), ink, S(1.0f));
		}
	}
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
	// A loaded icon is drawn as it is, so transparent parts show the banner. The panel-colored plate behind the
	// icon (and the placeholder tile) is only for while it loads or when the pack has none.
	const LoadedImage* icon = images.Get(pack.iconUrl);
	if (icon && FadeAlpha(*icon) >= 1.0f) {
		DrawImageCover(drawList, *icon, iconMin, ImVec2(iconMin.x + iconSize, iconMin.y + iconSize), S(12.0f), 1.0f);
	} else {
		drawList->AddRectFilled(ImVec2(iconMin.x - S(3.0f), iconMin.y - S(3.0f)),
			ImVec2(iconMin.x + iconSize + S(3.0f), iconMin.y + iconSize + S(3.0f)), ImGui::GetColorU32(panel), S(15.0f));
		DrawPackIcon(drawList, icon, pack, iconMin, iconSize, S(12.0f));
	}

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
		addChip(std::isdigit(static_cast<unsigned char>(pack.version[0])) ? "v" + pack.version : pack.version,
			Theme::C().textDim); // "1.2" reads "v1.2"; "v7.5.1" and "Beta 3" stay as they are

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

namespace Widgets {

namespace {
struct OpenCard {
	ImGuiID id = 0;
	float bottomPadding = 0.0f;
	bool measuringOffscreen = false; // pushed the invisible, unclipped state (see BeginCard)
};
std::vector<OpenCard> g_openCards;
}

void BeginCard(const char* id, ImVec2 padding, float width)
{
	const ImGuiID key = ImGui::GetID(id);
	const float remembered = ImGui::GetStateStorage()->GetFloat(key, 0.0f);
	const ImVec2 pad = padding.x >= 0.0f ? padding : ImGui::GetStyle().WindowPadding;
	// First time only: a guess, corrected on the next frame.
	const float height = remembered > 0.0f ? remembered : pad.y * 2.0f + ImGui::GetFrameHeightWithSpacing() * 2.0f;
	// A card below the visible area is skipped by ImGui and so never measured, which leaves a guessed
	// height that jumps when the card scrolls in (moving everything laid out after it). Measure it
	// once, unclipped and invisible (an alpha of exactly 0 would skip it again).
	const bool measuringOffscreen = remembered <= 0.0f && ImGui::GetCursorScreenPos().y > ImGui::GetCurrentWindow()->ClipRect.Max.y;
	if (measuringOffscreen) {
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.001f);
		ImGui::PushClipRect(ImVec2(-1.0e5f, -1.0e5f), ImVec2(1.0e5f, 1.0e5f), false);
	}
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::C().cardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
	ImGui::BeginChild(key, ImVec2(std::max(0.0f, width), height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	g_openCards.push_back({ key, pad.y, measuringOffscreen });
}

void EndCard()
{
	const OpenCard card = g_openCards.back();
	g_openCards.pop_back();
	// The cursor sits one item-spacing below the last item.
	const float contentBottom = ImGui::GetCursorPosY() + ImGui::GetScrollY() - ImGui::GetStyle().ItemSpacing.y;
	const float height = std::ceil(contentBottom + card.bottomPadding);
	const bool measured = !ImGui::GetCurrentWindow()->SkipItems;
	ImGui::EndChild();
	ImGui::PopStyleColor();
	if (card.measuringOffscreen) {
		ImGui::PopClipRect();
		ImGui::PopStyleVar();
	}
	if (measured)
		ImGui::GetStateStorage()->SetFloat(card.id, height);
}

}

namespace Widgets {

void LogoBlock(float size)
{
	static const std::vector<std::uint8_t> pixels = Logo::BlockPixels(Logo::kBaseSize);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(size, size));
	if (!ImGui::IsItemVisible())
		return;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float cell = size / Logo::kBaseSize;
	const float alpha = ImGui::GetStyle().Alpha;
	for (int y = 0; y < Logo::kBaseSize; ++y) {
		for (int x = 0; x < Logo::kBaseSize; ++x) {
			const std::uint8_t* p = &pixels[static_cast<std::size_t>((y * Logo::kBaseSize + x) * 4)];
			if (p[3] == 0)
				continue;
			// Rounded to whole screen pixels so neighbouring cells don't leave seams.
			const ImVec2 min(std::floor(pos.x + x * cell), std::floor(pos.y + y * cell));
			const ImVec2 max(std::floor(pos.x + (x + 1) * cell), std::floor(pos.y + (y + 1) * cell));
			drawList->AddRectFilled(min, max, IM_COL32(p[0], p[1], p[2], static_cast<int>(p[3] * alpha)));
		}
	}
}

void Wordmark(const char* text, float pixel, const ImVec4& face, const ImVec4& depth)
{
	const int columns = static_cast<int>(std::strlen(text)) * (Logo::kGlyphWidth + 1) - 1;
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float depthOffset = std::max(1.0f, std::round(pixel));
	ImGui::Dummy(ImVec2(columns * pixel + depthOffset, Logo::kGlyphHeight * pixel + depthOffset));
	if (!ImGui::IsItemVisible())
		return;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	// The extruded edge first (one copy per screen pixel of depth), then the faces on top.
	for (float shift = depthOffset; shift >= 0.0f; shift -= 1.0f) {
		const int pass = shift == 0.0f ? 1 : 0;
		float x = pos.x;
		for (const char* c = text; *c; ++c) {
			const auto rows = Logo::Glyph(static_cast<char>(std::toupper(static_cast<unsigned char>(*c))));
			for (int row = 0; row < Logo::kGlyphHeight; ++row) {
				for (int col = 0; col < Logo::kGlyphWidth; ++col) {
					if ((rows[static_cast<std::size_t>(row)] & (0x10 >> col)) == 0)
						continue;
					ImVec4 color = depth;
					if (pass == 1) {
						// Lighter at the top like the Minecraft title, with a little stone speckle.
						const float t = static_cast<float>(row) / (Logo::kGlyphHeight - 1);
						color = Theme::Mix(face, depth, t * 0.35f);
						if ((static_cast<unsigned>(row * 7 + col * 13 + (c - text) * 5) % 5) == 0)
							color = Theme::Mix(color, depth, 0.18f);
					}
					const ImVec2 min(std::floor(x + col * pixel + shift), std::floor(pos.y + row * pixel + shift));
					drawList->AddRectFilled(min, ImVec2(std::floor(x + (col + 1) * pixel + shift),
						std::floor(pos.y + (row + 1) * pixel + shift)), ImGui::GetColorU32(color));
				}
			}
			x += (Logo::kGlyphWidth + 1) * pixel;
		}
	}
}

}

namespace Widgets {

bool TabStrip(const char* id, const char* const* labels, int count, int* current)
{
	bool changed = false;
	ImGui::PushID(id);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(18.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(18.0f), S(8.0f)));
	for (int i = 0; i < count; ++i) {
		const bool selected = *current == i;
		if (i > 0)
			ImGui::SameLine();
		ImGui::PushID(i);
		ImGui::PushStyleColor(ImGuiCol_Button, selected ? Theme::C().accentDeep : Theme::C().frameBg);
		ImGui::PushFont(selected ? Theme::GetFonts().bold : Theme::GetFonts().regular, 0.0f);
		if (ImGui::Button(labels[i]) && !selected) {
			*current = i;
			changed = true;
		}
		ImGui::PopFont();
		ImGui::PopStyleColor();
		ImGui::PopID();
	}
	ImGui::PopStyleVar(2);
	ImGui::PopID();
	return changed;
}

}

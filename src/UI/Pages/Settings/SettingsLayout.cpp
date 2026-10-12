#include "UI/Pages/Settings/SettingsLayout.h"

#include "Platform/Platform.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

using Theme::S;

namespace {

constexpr float kMinColumnWidth = 520.0f; // unscaled

}

void SettingsLayout::BeginFrame(double shownAt)
{
	m_cardsShownAt = std::max(m_cardsShownAt, shownAt);
	m_cardIndex = 0;
}

void SettingsLayout::BeginFlow()
{
	m_flow = Flow{};
	const float available = ImGui::GetContentRegionAvail().x;
	m_flow.gap = S(14.0f);
	m_flow.columns = std::clamp(static_cast<int>((available + m_flow.gap) / (S(kMinColumnWidth) + m_flow.gap)), 1, 3);
	if (m_flow.columns < 2)
		return;
	m_flow.active = true;
	m_flow.columnWidth = std::floor((available - m_flow.gap * static_cast<float>(m_flow.columns - 1)) / static_cast<float>(m_flow.columns));
	m_flow.x = ImGui::GetCursorScreenPos().x;
}

float SettingsLayout::FlowBottom() const
{
	float bottom = m_flow.columnY[0];
	for (int i = 1; i < m_flow.columns; ++i)
		bottom = std::max(bottom, m_flow.columnY[i]);
	return bottom;
}

void SettingsLayout::BeginCard(const char* id, bool fullWidth)
{
	float width = 0.0f;
	if (m_flow.active) {
		const float cursorY = ImGui::GetCursorScreenPos().y;
		if (!m_flow.hasCard || cursorY > m_flow.afterCardY + 0.5f) {
			// First card, or content was drawn after the last one: a new section starts here.
			for (float& y : m_flow.columnY)
				y = cursorY;
		}
		m_flow.fullWidth = fullWidth;
		m_flow.column = 0;
		float y = m_flow.columnY[0];
		if (fullWidth) {
			y = FlowBottom();
			width = m_flow.columnWidth * static_cast<float>(m_flow.columns) + m_flow.gap * static_cast<float>(m_flow.columns - 1);
		} else {
			for (int i = 1; i < m_flow.columns; ++i) {
				if (m_flow.columnY[i] < m_flow.columnY[m_flow.column] - 0.5f)
					m_flow.column = i;
			}
			y = m_flow.columnY[m_flow.column];
			width = m_flow.columnWidth;
		}
		ImGui::SetCursorScreenPos(ImVec2(m_flow.x + (fullWidth ? 0.0f : (m_flow.columnWidth + m_flow.gap) * static_cast<float>(m_flow.column)), y));
	}
	Widgets::BeginEntrance(m_cardsShownAt, m_cardIndex++);
	Widgets::BeginCard(id, ImVec2(S(22.0f), S(18.0f)), width);
}

void SettingsLayout::EndCard()
{
	Widgets::EndCard();
	Widgets::EndEntrance();
	if (!m_flow.active) {
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		return;
	}
	// The card's own height, not its (animated) position: cards rise into place.
	const float top = m_flow.fullWidth ? FlowBottom() : m_flow.columnY[m_flow.column];
	const float bottom = top + ImGui::GetItemRectSize().y + m_flow.gap;
	if (m_flow.fullWidth) {
		for (float& y : m_flow.columnY)
			y = bottom;
	} else {
		m_flow.columnY[m_flow.column] = bottom;
	}
	// Leave the cursor under every column, so whatever comes next starts below them.
	ImGui::SetCursorScreenPos(ImVec2(m_flow.x, FlowBottom()));
	ImGui::Dummy(ImVec2(0.0f, 0.0f));
	m_flow.afterCardY = ImGui::GetCursorScreenPos().y;
	m_flow.hasCard = true;
}

void SettingsLayout::CardTitle(const char* title, const char* subtitle)
{
	Widgets::Text(Theme::GetFonts().bold, Theme::kFontHeading, Theme::C().text, title);
	if (subtitle) {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", subtitle);
		ImGui::PopTextWrapPos();
	}
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
}

float m_labelWidth = 0.0f; // the open rows' label column

bool SettingsLayout::BeginRows(const char* id)
{
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(6.0f), S(9.0f)));
	if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
		ImGui::PopStyleVar();
		return false;
	}
	m_labelWidth = std::min(S(240.0f), std::floor(ImGui::GetContentRegionAvail().x * 0.4f));
	ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, m_labelWidth);
	ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
	return true;
}

void SettingsLayout::Row(const char* label, const char* hint)
{
	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::AlignTextToFramePadding();
	ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
	ImGui::TextUnformatted(label);
	ImGui::PopFont();
	if (hint) {
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + m_labelWidth - S(20.0f));
		ImGui::PushFont(nullptr, Theme::kFontSmall);
		ImGui::TextDisabled("%s", hint);
		ImGui::PopFont();
		ImGui::PopTextWrapPos();
	}
	ImGui::TableNextColumn();
	ImGui::SetNextItemWidth(-FLT_MIN);
}

void SettingsLayout::EndRows()
{
	ImGui::EndTable();
	ImGui::PopStyleVar();
}

bool SettingsLayout::Notice(const char* id, const char* text, const ImVec4& tint)
{
	ImGui::PushID(id);
	bool dismissed = false;
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->ChannelsSplit(2);
	drawList->ChannelsSetCurrent(1);
	ImGui::SetCursorScreenPos(ImVec2(pos.x + S(16.0f), pos.y + S(10.0f)));
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - S(32.0f) - S(28.0f));
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
	const float height = ImGui::GetItemRectMax().y - pos.y + S(10.0f);
	if (Widgets::CloseX("##hide_notice", ImVec2(pos.x + width - S(30.0f), pos.y + S(8.0f)), S(22.0f)))
		ImGui::OpenPopup("Hide this tip?");
	drawList->ChannelsSetCurrent(0);
	drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(Theme::WithAlpha(tint, 0.14f)), S(8.0f));
	drawList->AddRectFilled(pos, ImVec2(pos.x + S(4.0f), pos.y + height), ImGui::GetColorU32(tint), S(2.0f));
	drawList->ChannelsMerge();
	ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height));
	ImGui::Dummy(ImVec2(width, S(8.0f)));

	const ImVec2 display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Hide this tip?", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted("It won't come back on later launches.");
		ImGui::Spacing();
		if (ImGui::Button("Don't show again")) {
			dismissed = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::PopID();
	return dismissed;
}

void SettingsLayout::FolderRow(const char* label, const char* hint, const std::string& path)
{
	Row(label, hint);
	const float buttonWidth = ImGui::CalcTextSize("Open").x + ImGui::GetStyle().FramePadding.x * 2.0f;
	std::string display = path;
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
	ImGui::PushID(label);
	ImGui::InputText("##path", &display, ImGuiInputTextFlags_ReadOnly);
	ImGui::SameLine();
	if (ImGui::Button("Open"))
		Platform::OpenFolder(path);
	ImGui::PopID();
}

void SettingsLayout::TextRow(const char* label, const char* text)
{
	Row(label);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(text);
}

std::string SettingsLayout::FormatGb(int mb)
{
	char text[32];
	std::snprintf(text, sizeof(text), "%.1f GB", mb / 1024.0);
	return text;
}

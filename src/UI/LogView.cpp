#include "LogView.h"

#include "Theme.h"
#include "Widgets.h"

#include "imgui_internal.h" // InnerRect, to leave the scrollbars alone

#include <algorithm>
#include <cmath>

using Theme::S;

namespace {

ImVec4 LineColor(const std::string& line)
{
	if (line.find("ERROR") != std::string::npos || line.find("FATAL") != std::string::npos ||
		line.find("Exception") != std::string::npos)
		return Theme::C().error;
	if (line.find("WARN") != std::string::npos)
		return Theme::Mix(Theme::C().error, Theme::C().text, 0.5f);
	return Theme::C().text;
}

bool IsContinuation(char c)
{
	return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// The byte offset in text nearest to x pixels from its start.
std::size_t ColumnAt(ImFont* font, float size, const std::string& text, float x)
{
	float width = 0.0f;
	std::size_t i = 0;
	while (i < text.size()) {
		std::size_t next = i + 1;
		while (next < text.size() && IsContinuation(text[next]))
			++next;
		const float charWidth = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data() + i, text.data() + next).x;
		if (x < width + charWidth * 0.5f)
			return i;
		width += charWidth;
		i = next;
	}
	return text.size();
}

float WidthTo(ImFont* font, float size, const std::string& text, std::size_t column)
{
	column = std::min(column, text.size());
	return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + column).x;
}

}

std::string LogView::Selected(const std::vector<std::string>& lines, std::uint64_t firstLine) const
{
	if (!m_hasSelection || lines.empty())
		return {};
	Pos start = m_anchor, end = m_cursor;
	if (Before(end, start))
		std::swap(start, end);
	std::string out;
	for (std::uint64_t line = std::max(start.line, firstLine); line <= end.line && line < firstLine + lines.size(); ++line) {
		const std::string& text = lines[static_cast<std::size_t>(line - firstLine)];
		const std::size_t from = line == start.line ? std::min(start.column, text.size()) : 0;
		const std::size_t to = line == end.line ? std::min(end.column, text.size()) : text.size();
		if (line != std::max(start.line, firstLine))
			out += '\n';
		if (to > from)
			out.append(text, from, to - from);
	}
	return out;
}

void LogView::Draw(const char* id, const std::vector<std::string>& lines, ImVec2 size, std::uint64_t firstLine)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Mix(Theme::C().windowBg, Theme::C().cardBg, 0.4f));
	Widgets::BeginScrollArea(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
	ImFont* font = ImGui::GetFont();
	const float fontSize = ImGui::GetFontSize();
	const float lineHeight = std::floor(ImGui::GetTextLineHeight() + S(3.0f));
	const std::uint64_t end = firstLine + lines.size();

	// Follow new output while the view is at the bottom.
	const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - lineHeight;
	if (end != m_lastEnd && atBottom)
		m_scrollToBottom = true;
	m_lastEnd = end;

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float visibleTop = ImGui::GetScrollY();
	const float visibleHeight = ImGui::GetWindowHeight();
	const std::size_t first = static_cast<std::size_t>(std::max(0.0f, std::floor(visibleTop / lineHeight) - 1.0f));
	const std::size_t last = std::min(lines.size(), static_cast<std::size_t>((visibleTop + visibleHeight) / lineHeight) + 2);

	// Mouse: click-drag selects, double-click selects a line.
	const ImVec2 mouse = ImGui::GetMousePos();
	auto posAt = [&](ImVec2 at) {
		Pos pos;
		if (lines.empty())
			return pos;
		const float row = std::floor((at.y - origin.y) / lineHeight);
		const std::size_t index = static_cast<std::size_t>(std::clamp(row, 0.0f, static_cast<float>(lines.size() - 1)));
		pos.line = firstLine + index;
		pos.column = ColumnAt(font, fontSize, lines[index], at.x - origin.x);
		return pos;
	};
	const ImRect inner = ImGui::GetCurrentWindow()->InnerRect; // the text area without its scrollbars
	const bool hovered = ImGui::IsWindowHovered() && inner.Contains(mouse);
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !lines.empty()) {
		m_anchor = m_cursor = posAt(mouse);
		m_hasSelection = true;
		m_selecting = true;
		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			m_anchor.column = 0;
			m_cursor.column = lines[static_cast<std::size_t>(m_anchor.line - firstLine)].size();
			m_selecting = false;
		}
	}
	if (m_selecting) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			m_cursor = posAt(mouse);
			// Dragging past the top or bottom edge scrolls.
			const float top = ImGui::GetWindowPos().y;
			const float bottom = top + visibleHeight;
			if (mouse.y < top)
				ImGui::SetScrollY(ImGui::GetScrollY() - lineHeight);
			else if (mouse.y > bottom)
				ImGui::SetScrollY(ImGui::GetScrollY() + lineHeight);
		} else {
			m_selecting = false;
		}
	}
	if (m_hasSelection && m_anchor.line == m_cursor.line && m_anchor.column == m_cursor.column && !m_selecting)
		m_hasSelection = false;

	Pos selStart = m_anchor, selEnd = m_cursor;
	if (Before(selEnd, selStart))
		std::swap(selStart, selEnd);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImU32 selectionColor = ImGui::GetColorU32(Theme::WithAlpha(Theme::C().accent, 0.32f));
	for (std::size_t i = first; i < last; ++i) {
		const std::string& text = lines[i];
		const std::uint64_t line = firstLine + i;
		const ImVec2 pos(origin.x, origin.y + static_cast<float>(i) * lineHeight);
		if (m_hasSelection && line >= selStart.line && line <= selEnd.line) {
			const float x0 = line == selStart.line ? WidthTo(font, fontSize, text, selStart.column) : 0.0f;
			const float x1 = line == selEnd.line ? WidthTo(font, fontSize, text, selEnd.column)
				: WidthTo(font, fontSize, text, text.size()) + S(6.0f);
			drawList->AddRectFilled(ImVec2(pos.x + x0, pos.y), ImVec2(pos.x + x1, pos.y + lineHeight), selectionColor, S(2.0f));
		}
		drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y + S(1.5f)), ImGui::GetColorU32(LineColor(text)),
			text.data(), text.data() + text.size());
		m_maxWidth = std::max(m_maxWidth, WidthTo(font, fontSize, text, text.size()));
	}
	ImGui::Dummy(ImVec2(std::max(m_maxWidth + S(8.0f), 1.0f), std::max(1.0f, lines.size() * lineHeight)));
	if (lines.empty())
		m_maxWidth = 0.0f;

	// Keyboard and context menu.
	const bool focused = ImGui::IsWindowFocused();
	if (focused && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A) && !lines.empty()) {
		m_anchor = { firstLine, 0 };
		m_cursor = { end - 1, lines.back().size() };
		m_hasSelection = true;
	}
	if (focused && m_hasSelection && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
		ImGui::SetClipboardText(Selected(lines, firstLine).c_str());
	if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		ImGui::OpenPopup("##log_menu");
	if (ImGui::BeginPopup("##log_menu")) {
		if (Widgets::MenuItem("Copy", "Ctrl+C", m_hasSelection))
			ImGui::SetClipboardText(Selected(lines, firstLine).c_str());
		if (Widgets::MenuItem("Select all", "Ctrl+A", !lines.empty())) {
			m_anchor = { firstLine, 0 };
			m_cursor = { end - 1, lines.back().size() };
			m_hasSelection = true;
		}
		if (Widgets::MenuItem("Copy everything", nullptr, !lines.empty())) {
			std::string all;
			for (std::size_t i = 0; i < lines.size(); ++i)
				all += (i ? "\n" : "") + lines[i];
			ImGui::SetClipboardText(all.c_str());
		}
		ImGui::EndPopup();
	}

	if (m_scrollToBottom) {
		ImGui::SetScrollY(std::max(0.0f, lines.size() * lineHeight - visibleHeight + ImGui::GetStyle().WindowPadding.y * 2.0f + S(20.0f)));
		m_scrollToBottom = false;
	}
	Widgets::EndScrollArea();
	ImGui::PopStyleColor();
}

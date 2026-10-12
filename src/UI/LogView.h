#pragma once

#include "imgui.h"

#include <cstdint>
#include <string>
#include <vector>

// A read-only text view for logs and consoles. Only the visible lines are drawn, so it
// handles long logs. Text can be selected with the mouse (double-click selects a line)
// and copied with Ctrl+C or the right-click menu; Ctrl+A selects everything. ERROR and
// WARN lines are tinted. While the view is scrolled to the bottom it follows new lines.
class LogView {
public:
	// lines[0] is line number firstLine. A console that drops its oldest lines raises
	// firstLine by as many, so a selection stays on the same text.
	void Draw(const char* id, const std::vector<std::string>& lines, ImVec2 size, std::uint64_t firstLine = 0);

	void ScrollToBottom() { m_scrollToBottom = true; }
	void ClearSelection() { m_hasSelection = false; }

private:
	struct Pos {
		std::uint64_t line = 0; // absolute line number
		std::size_t column = 0; // byte offset in the line
	};
	static bool Before(const Pos& a, const Pos& b) { return a.line < b.line || (a.line == b.line && a.column < b.column); }
	std::string Selected(const std::vector<std::string>& lines, std::uint64_t firstLine) const;

	Pos m_anchor;
	Pos m_cursor;
	bool m_hasSelection = false;
	bool m_selecting = false;
	bool m_scrollToBottom = true;
	float m_maxWidth = 0.0f;
	std::uint64_t m_lastEnd = 0; // firstLine + line count when last drawn
};

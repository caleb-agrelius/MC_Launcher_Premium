#pragma once

#include "imgui.h"

#include <string>
#include <utility>

// How the Settings page lays out its cards and rows. Cards flow into columns on wide windows:
// each card goes to the shortest column (using the height BeginCard remembers), so the page
// needs less scrolling. Content drawn between cards starts a new section below every column.
class SettingsLayout {
public:
	// Call once a frame before drawing tabs. shownAt is when the page opened (cards bubble in
	// from the later of that and the last tab switch).
	void BeginFrame(double shownAt);
	void TabSwitched(double now) { m_cardsShownAt = now; }

	// Lays the cards drawn until EndFlow() out in as many columns as fit (up to three).
	void BeginFlow();
	void EndFlow() { m_flow.active = false; }

	// fullWidth spans every column (wide content such as the theme grid).
	void BeginCard(const char* id, bool fullWidth = false);
	void EndCard();
	void CardTitle(const char* title, const char* subtitle = nullptr);

	// Two-column label/control rows inside a card.
	bool BeginRows(const char* id);
	// Starts a row: label (and an optional hint under it) on the left; leaves the cursor in
	// the control column with the next item set to fill it.
	void Row(const char* label, const char* hint = nullptr);
	void EndRows();
	// A row whose control is plain text.
	void TextRow(const char* label, const char* text);
	// A read-only path with an "Open" button.
	void FolderRow(const char* label, const char* hint, const std::string& path);

	// A card with a title and label/control rows: body draws the rows.
	template <class Body>
	void RowsCard(const char* id, const char* title, Body&& body, bool fullWidth = false)
	{
		RowsCard(id, title, nullptr, std::forward<Body>(body), fullWidth);
	}
	template <class Body>
	void RowsCard(const char* id, const char* title, const char* subtitle, Body&& body, bool fullWidth = false)
	{
		BeginCard(id, fullWidth);
		CardTitle(title, subtitle);
		if (BeginRows((std::string(id) + "_rows").c_str())) {
			body();
			EndRows();
		}
		EndCard();
	}

	// A tinted notice box, e.g. for placeholder sections.
	void Notice(const char* text, const ImVec4& tint);

	// "1.5 GB" from megabytes.
	static std::string FormatGb(int mb);

private:
	struct Flow {
		bool active = false;
		int columns = 1;
		float columnWidth = 0.0f;
		float gap = 0.0f;
		float x = 0.0f;           // left edge, in screen space
		float columnY[3] = {};    // where each column's next card goes
		float afterCardY = -1.0f; // where EndCard left the cursor; anything lower means content came between
		int column = 0;           // the open card's column
		bool fullWidth = false;   // the open card spans every column
	};
	float FlowBottom() const; // the bottom of the tallest column

	Flow m_flow;
	double m_cardsShownAt = 0.0;
	int m_cardIndex = 0;      // cards drawn so far this frame, for the stagger
	float m_labelWidth = 0.0f; // the open rows' label column
};

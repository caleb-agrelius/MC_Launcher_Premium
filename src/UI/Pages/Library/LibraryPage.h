#pragma once

#include "UI/Pages/IPage.h"
#include "UI/Pages/Library/LibraryPackCard.h"

class PackLibrary;

// My Library: a card per installed pack, or an invitation to Discover when there are none.
class LibraryPage final : public IPage {
public:
	LibraryPage(PackLibrary& library, LibraryPackCard& card, INavigator& navigator, IDialogs& dialogs)
		: m_library(library), m_card(card), m_navigator(navigator), m_dialogs(dialogs) {}

	void DrawHeader() override;
	void Draw(const PageFrame& frame) override;

private:
	PackLibrary& m_library;
	LibraryPackCard& m_card;
	INavigator& m_navigator;
	IDialogs& m_dialogs;
};

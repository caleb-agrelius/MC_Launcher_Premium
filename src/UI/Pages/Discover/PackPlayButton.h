#pragma once

#include "Model/Modpack.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Play/PlayService.h"
#include "UI/Navigation.h"

// The big PLAY button under a Discover result. It plays the version shown here when that's
// installed (making it the active one), else the pack's active version, and is off for packs not
// in My Library. Progress and errors show on My Library, which it opens.
class PackPlayButton {
public:
	PackPlayButton(PackLibrary& library, PlayService& plays, INavigator& navigator)
		: m_library(library), m_plays(plays), m_navigator(navigator) {}

	void Draw(const Modpack& pack);

private:
	PackLibrary& m_library;
	PlayService& m_plays;
	INavigator& m_navigator;
};

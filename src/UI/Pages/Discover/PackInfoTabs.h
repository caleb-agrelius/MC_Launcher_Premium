#pragma once

#include "Model/Modpack.h"
#include "Services/Discover/PackDetailsService.h"

#include <string>

// The Description / Mods tabs at the bottom of Discover's details pane. The description and the
// mod list are fetched the first time their tab is shown (CurseForge's mod list waits for a click).
class PackInfoTabs {
public:
	explicit PackInfoTabs(PackDetailsService& details) : m_details(details) {}

	// Draws the tab strip and the open tab. True when the player switched tabs this frame.
	bool Draw(const Modpack& pack);

private:
	void DrawDescription(const Modpack& pack);
	void DrawMods(const Modpack& pack);

	PackDetailsService& m_details;
	int m_tab = 0;
	std::string m_modsFilter;
};

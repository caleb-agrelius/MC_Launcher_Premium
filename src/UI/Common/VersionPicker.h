#pragma once

#include "Model/Modpack.h"
#include "Network/PackSources.h"
#include "Services/Discover/PackDetailsService.h"

#include <string>

// A dropdown of a pack's versions, shared by Discover's install card and the Servers page.
// The list is fetched the first time the dropdown opens (see PackDetailsService).
class VersionPicker {
public:
	explicit VersionPicker(PackDetailsService& details) : m_details(details) {}

	// Draws the dropdown. preview is the closed text and currentId (a sourceVersionId) is
	// highlighted. Returns true when the player picked a version, which fills picked.
	bool Combo(const char* id, const Modpack& pack, const std::string& preview, const std::string& currentId,
		PackVersionInfo& picked);

	// Puts a picked version on a pack. Its Minecraft version replaces the old one even
	// when unknown, so the pack doesn't show the previous version's.
	static void Apply(Modpack& target, const PackVersionInfo& v);

	// True when v is the version a library pack has installed. Modrinth packs installed
	// before versions had ids only know their version number.
	static bool IsInstalled(const Modpack& pack, const PackVersionInfo& v);

private:
	PackDetailsService& m_details;
};

#pragma once

#include "Model/Modpack.h"
#include "Services/Discover/DiscoverModel.h"
#include "Services/Discover/OptionalModsService.h"
#include "Services/Discover/PackDetailsService.h"
#include "Services/Downloads/DownloadService.h"
#include "Services/Library/PackLibrary.h"
#include "UI/Common/VersionPicker.h"

// The part of Discover's details pane that installs the pack. Whole-pack sources (CurseForge, FTB,
// Technic, ATLauncher) get one card for the chosen version; the others list the version's files,
// each with its own Install button.
class PackInstallSection {
public:
	PackInstallSection(PackDetailsService& details, DownloadService& downloads, PackLibrary& library, VersionPicker& versions,
		IInstallRequester& installer)
		: m_details(details), m_downloads(downloads), m_library(library), m_versions(versions), m_installer(installer) {}

	// pack is the selected result (picking another version changes it) and view the source it came from.
	// compact (the grid view's modal): no section labels, and a pack with one file gets a single
	// card holding the version picker and that file.
	void Draw(SourceView& view, Modpack& pack, bool compact = false);

private:
	void DrawPlanInstallCard(SourceView& view, Modpack& pack, bool compact);
	void DrawFileCards(SourceView& view, Modpack& pack, bool compact);
	// "Version [picker]  Minecraft x . loader" for a source that installs file by file.
	void DrawVersionRow(Modpack& pack, float comboWidth);
	// Stand-ins with the loaded layout's sizes while the pack's versions and files load, so the PLAY
	// button and everything below stay put when they arrive.
	void DrawVersionRowPlaceholder(float comboWidth);
	void DrawFileRowPlaceholder();
	// One file: name, size and its Install / progress / Installed state. Draws inside a card.
	void DrawFileRow(Modpack& pack, const PackFile& file);

	PackDetailsService& m_details;
	DownloadService& m_downloads;
	PackLibrary& m_library;
	VersionPicker& m_versions;
	IInstallRequester& m_installer;
};

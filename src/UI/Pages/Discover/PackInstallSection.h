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
	void Draw(SourceView& view, Modpack& pack);

private:
	void DrawPlanInstallCard(SourceView& view, Modpack& pack);
	void DrawFileCards(SourceView& view, Modpack& pack);

	PackDetailsService& m_details;
	DownloadService& m_downloads;
	PackLibrary& m_library;
	VersionPicker& m_versions;
	IInstallRequester& m_installer;
};

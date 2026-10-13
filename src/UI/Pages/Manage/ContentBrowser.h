#pragma once

#include "Network/ModSearch.h"
#include "UI/Pages/Manage/ContentModel.h"
#include "UI/Pages/Manage/ManageTypes.h"

#include <map>
#include <set>
#include <string>
#include <vector>

// Content > Browse: search Modrinth or CurseForge (the player's choice; CurseForge needs an API key)
// for mods, resource packs or shader packs that fit the pack's Minecraft version and loader, and
// install one with what it needs.
class ContentBrowser {
public:
	explicit ContentBrowser(ContentModel& model) : m_model(model) {}

	// Draws the browser for the content kind at index kind. False when the player went back to
	// the installed list.
	bool Draw(const ManagePageContext& context, int kind);

private:
	// The search for the shown content kind, filtered to the pack's Minecraft version and loader.
	ContentSearch BrowseSearch(const ManagePageContext& context) const;
	void Search(const ManagePageContext& context);
	// Which projects of the shown source the pack has already (by file hash or fingerprint).
	void LoadInstalledProjects(const ManagePageContext& context);
	// The Modrinth and CurseForge buttons; true if the player picked the other source.
	bool DrawSourceChips();
	void InstallProject(const ManagePageContext& context, const ContentProject& project);

	ContentModel& m_model;
	int m_kind = 0;
	ContentSource m_source = ContentSource::Modrinth;
	std::string m_query;
	double m_editedAt = 0.0;     // when the query last changed; searches wait a moment
	std::string m_searchedFor;   // the search the results belong to
	std::vector<ContentProject> m_results;
	bool m_searching = false;
	std::string m_searchError;
	int m_generation = 0;        // bumped per search, so late results are ignored
	std::set<std::string> m_installedProjects; // projects the pack has, from either source (their ids differ)
	std::string m_installedFor;  // game folder m_installedProjects belongs to
	std::set<ContentSource> m_installedLoaded; // sources m_installedProjects has been read for, for that folder
	std::set<std::string> m_installing;        // project ids being installed
	std::map<std::string, std::string> m_installErrors;
};

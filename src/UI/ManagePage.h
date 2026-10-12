#pragma once

#include "Game/InstanceContent.h"
#include "Model/Modpack.h"
#include "Network/ModSearch.h"
#include "UI/BackgroundTasks.h"
#include "UI/LogView.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class HttpClient;
class ImageCache;

// What the manage page needs from the app for the pack it shows. Rebuilt every frame.
struct ManagePageContext {
	const Modpack* pack = nullptr;      // the My Library pack
	std::filesystem::path gameDir;      // its active version's game folder
	std::filesystem::path installDir;   // and install folder
	HttpClient* http = nullptr;
	ImageCache* images = nullptr;
	BackgroundTasks tasks;
	bool showGalleryBanners = true;
	bool hasLaunchSettings = false; // the pack has its own launch settings
	bool canHostServer = false;
	bool canRepair = true;          // false for packs the player made (nothing to re-download)
	double shownAt = 0.0; // when the page opened, for the entrance animation
	// Play state.
	bool playBusy = false;
	bool playing = false;
	bool playCancelling = false; // Cancel was pressed; the worker hasn't stopped yet
	std::string playStep;
	std::string playError;
	double playProgress = -1.0;
	// The player's edits to the active version (InstalledVersion::contentChanges), and how
	// to record a new one (main.cpp saves it in library.json).
	const ContentChanges* changes = nullptr;
	std::function<void(const std::string& path, ContentChange change)> recordChange;
	// Files and folders dropped on the window this frame; they're added as content.
	std::vector<std::filesystem::path> droppedPaths;
};

// What the player asked for; main.cpp carries it out.
enum class ManageAction {
	None,
	Back,           // to My Library
	Play,
	CancelPlay,
	StopGame,
	LaunchSettings,
	Repair,
	Server,         // the pack's server on the Servers page
	Uninstall,      // the whole pack (versionKey empty) or one version
	SelectVersion,  // make versionKey the active version
	UndoChanges,    // undo the player's edits to the active version, then repair its files
};

struct ManageRequest {
	ManageAction action = ManageAction::None;
	std::string versionKey;
};

// The full-page view of one My Library pack: Overview, Content (mods, resource packs and
// shader packs, switched on and off natively), Changelog, Logs (selectable and copyable)
// and Screenshots. Keep one for the app's lifetime; background work posts back to it.
class ManagePage {
public:
	ManageRequest Draw(const ManagePageContext& context);
	// Forgets cached folder listings, e.g. after the active version changed or files were installed.
	void Refresh() { m_loadedFor.clear(); }
	// Opens Content > Mods with the Modrinth browser, e.g. for a pack that was just created.
	void ShowBrowse();

private:
	struct ContentList {
		std::vector<ContentFile> files;
		std::string error;
	};
	struct LogFile {
		std::string label;            // e.g. "latest.log", "crash-reports/crash-....txt"
		std::filesystem::path path;
		std::uint64_t size = 0;
	};
	struct Screenshot {
		std::filesystem::path path;
		std::string label; // file name
	};

	void Reload(const ManagePageContext& context);
	// Re-reads only the content folders (after adding files), keeping logs and screenshots.
	void ReloadContent(const ManagePageContext& context);
	// Copies files into the game folder as mods, resource packs or shader packs.
	void AddFiles(const ManagePageContext& context, const std::vector<std::filesystem::path>& paths);
	void ScanMetadata(const ManagePageContext& context);
	// Modrinth: the search for the shown content kind, filtered to the pack's Minecraft version
	// and loader; running it; which projects the pack has; and installing one.
	ContentSearch BrowseSearch(const ManagePageContext& context) const;
	void SearchModrinth(const ManagePageContext& context);
	void LoadInstalledProjects(const ManagePageContext& context);
	void InstallProject(const ManagePageContext& context, const ContentProject& project);
	void LoadLog(const ManagePageContext& context, std::size_t index);
	void LoadChangelog(const ManagePageContext& context);

	ManageRequest DrawHeader(const ManagePageContext& context);
	ManageRequest DrawOverview(const ManagePageContext& context);
	void DrawContent(const ManagePageContext& context);
	void DrawBrowse(const ManagePageContext& context);
	void DrawChangelog(const ManagePageContext& context);
	void DrawLogs(const ManagePageContext& context);
	void DrawScreenshots(const ManagePageContext& context);
	void DrawScreenshotViewer(const ManagePageContext& context);

	std::string m_loadedFor; // pack id + game folder the lists below belong to
	int m_tab = 0;
	double m_tabShownAt = 0.0;

	// Content.
	int m_contentKind = 0;
	ContentList m_content[3];
	std::string m_contentFilter;
	std::string m_contentError;
	std::string m_contentNotice; // e.g. "Added sodium.jar."
	std::string m_removeName; // content whose "Remove" confirmation is open
	bool m_confirmUndo = false; // the "Undo my changes" confirmation is open
	bool m_reloadContent = false; // background installs finished; re-read the folders next frame

	// Browsing Modrinth (Content > Browse Modrinth).
	bool m_browsing = false;
	std::string m_browseQuery;
	double m_browseEditedAt = 0.0;     // when the query last changed; searches wait a moment
	std::string m_searchedFor;         // the search the results belong to
	std::vector<ContentProject> m_results;
	bool m_searching = false;
	std::string m_searchError;
	int m_browseGeneration = 0;        // bumped per search, so late results are ignored
	std::set<std::string> m_installedProjects; // Modrinth projects the pack has (by file hash)
	std::string m_installedFor;        // game folder m_installedProjects belongs to
	std::set<std::string> m_installing;          // project ids being installed
	std::map<std::string, std::string> m_installErrors;
	// Mod metadata by jar file name, filled by a background scan.
	std::shared_ptr<std::map<std::string, ModMetadata>> m_metadata;
	bool m_scanning = false;
	int m_generation = 0; // bumped on Reload, so late background results are ignored
	int m_scanGeneration = 0; // bumped when the content is re-read, so an older metadata scan is ignored

	// Overview stats.
	int m_worlds = 0;
	std::string m_lastPlayed;

	// Changelog, by pack id + version id.
	std::map<std::string, std::string> m_changelogs;
	std::map<std::string, std::string> m_changelogErrors;
	std::set<std::string> m_changelogsLoading;

	// Logs.
	std::vector<LogFile> m_logs;
	std::size_t m_logIndex = 0;
	std::vector<std::string> m_logLines;
	std::vector<std::string> m_shownLines; // m_logLines matching the filter
	std::string m_logFilter;
	std::string m_shownFilter;
	bool m_refilter = false; // m_shownLines needs rebuilding (new lines loaded)
	bool m_logLoading = false;
	std::string m_logError;
	LogView m_logView;
	float m_logListWidth = 250.0f; // unscaled; the splitter beside the list changes it

	// Screenshots, newest first.
	std::vector<Screenshot> m_screenshots;
	int m_viewing = -1; // screenshot open in the viewer
};

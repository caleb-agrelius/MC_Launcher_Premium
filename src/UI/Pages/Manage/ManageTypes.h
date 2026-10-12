#pragma once

#include "Util/Text.h"

#include "Game/InstanceContent.h"
#include "Model/Modpack.h"
#include "UI/BackgroundTasks.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class IHttpClient;
class ImageCache;

// What the manage page needs from the app for the pack it shows. Rebuilt every frame.
struct ManagePageContext {
	const Modpack* pack = nullptr;      // the My Library pack
	std::filesystem::path gameDir;      // its active version's game folder
	std::filesystem::path installDir;   // and install folder
	IHttpClient* http = nullptr;
	ImageCache* images = nullptr;
	BackgroundTasks tasks;
	bool showGalleryBanners = true;
	bool hasLaunchSettings = false; // the pack has its own launch settings
	bool canHostServer = false;
	bool canRepair = true;          // false for packs the player made (nothing to re-download)
	bool canUpdate = false;         // the pack's source can be asked for its newest version
	// An update is installing a newer version (UpdateService); updateStatus says how far.
	bool updating = false;
	std::string updateStatus;
	double shownAt = 0.0; // when the page opened, for the entrance animation
	// Play state.
	bool playBusy = false;
	bool playing = false;
	bool playCancelling = false; // Cancel was pressed; the worker hasn't stopped yet
	std::string playStep;
	std::string playError;
	double playProgress = -1.0;
	// The player's edits to the active version (InstalledVersion::contentChanges), and how
	// to record a new one (PackContentService saves it in library.json).
	const ContentChanges* changes = nullptr;
	std::function<void(const std::string& path, ContentChange change)> recordChange;
	// Files and folders dropped on the window this frame; they're added as content.
	std::vector<std::filesystem::path> droppedPaths;
};

// What the player asked for; ManagePageHost carries it out.
enum class ManageAction {
	None,
	Back,           // to My Library
	Play,
	CancelPlay,
	StopGame,
	LaunchSettings,
	Repair,
	Update,         // check the source for a newer version (the "Update pack" dialog)
	Server,         // the pack's server on the Servers page
	Uninstall,      // the whole pack (versionKey empty) or one version
	SelectVersion,  // make versionKey the active version
	UndoChanges,    // undo the player's edits to the active version, then repair its files
};

struct ManageRequest {
	ManageAction action = ManageAction::None;
	std::string versionKey;
};

// What a tab hands back to the page while drawing.
struct ManageFrame {
	ManageRequest request;  // something for ManagePageHost to carry out
	bool reload = false;    // read the pack's folders again
	int showTab = -1;       // switch to this tab, when not negative
};

// One tab of the Manage page. A tab owns what it shows and loads it in Reload(), which runs
// when the pack (or its active version) changes and when something asks for a refresh.
class IManageTab {
public:
	virtual ~IManageTab() = default;
	virtual void Reload(const ManagePageContext& context) = 0;
	// The tab became the visible one (also right after a Reload while it is visible).
	virtual void OnShown(const ManagePageContext&) {}
	virtual void Draw(const ManagePageContext& context, ManageFrame& frame) = 0;
};

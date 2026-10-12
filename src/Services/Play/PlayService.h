#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Core/WindowControl.h"
#include "Model/Modpack.h"
#include "Services/Account/AccountService.h"
#include "Services/Downloads/DownloadService.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Settings/SettingsStore.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

// What one installed version is doing as far as playing goes. Keyed by PlayService::Key().
struct PlayState {
	bool busy = false;    // preparing or running
	bool playing = false; // Minecraft has started
	std::string step;
	double progress = -1.0;
	std::string error;
	std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
	std::shared_ptr<std::atomic<bool>> stop = std::make_shared<std::atomic<bool>>(false); // force-close the game
};

// Preparing, repairing and running Minecraft for library packs. One entry per installed
// version, so two versions of a pack can be prepared or played at the same time. The states
// are shared with the workers under a mutex; everything else is UI thread only.
class PlayService {
public:
	static constexpr const char* kCancellingStep = "Cancelling...";

	PlayService(const Runtime& runtime, SettingsStore& settings, PackLibrary& library, AccountService& accounts,
		DownloadService& downloads, IWindowControl& window);

	// "<pack id>@<version key>" for a pack's active installed version.
	std::string Key(const Modpack& pack) const;

	// A copy of one version's state (a fresh one if it has none).
	PlayState StateOf(const std::string& key) const;
	// True when the given installed version (or, with an empty key, any version of the pack)
	// is being prepared, repaired or played.
	bool VersionBusy(const std::string& packId, const std::string& versionKey) const;
	// True while any pack is being prepared or played.
	bool AnyBusy() const;

	// Prepares the pack's active version and launches Minecraft. Needs an account.
	void Start(const Modpack& pack);
	// Re-checks a library pack's files and re-downloads damaged or missing ones. Whole-pack
	// installs re-run their install plan, which keeps files whose hash still matches.
	void Repair(const Modpack& pack);
	// Asks a version's play or repair worker (by key) to stop; the step shows "Cancelling..."
	// until it has.
	void Cancel(const std::string& key);
	// Force-closes a version's running game.
	void Stop(const std::string& key);
	// Stops preparing packs (at shutdown). A running Minecraft keeps running.
	void CancelAll();
	// Forgets the removed versions' state (all of the pack's for the whole pack).
	void Forget(const std::string& packId, bool wholePack, const std::string& versionKey);

private:
	void RecordError(const std::string& key, const std::string& message);

	Runtime m_rt;
	SettingsStore& m_settings;
	PackLibrary& m_library;
	AccountService& m_accounts;
	DownloadService& m_downloads;
	IWindowControl& m_window;
	mutable std::mutex m_mutex;
	std::map<std::string, PlayState> m_plays;
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};

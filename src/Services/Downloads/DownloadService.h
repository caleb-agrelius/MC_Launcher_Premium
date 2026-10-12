#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Services/Downloads/DownloadState.h"
#include "Services/Library/PackLibrary.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Downloading pack files and installing packs into the library: the file-by-file path
// (download a .mrpack, extract it) and the whole-pack path (a source's install plan).
// Progress lives in a map the pages read while workers update it, under a mutex. Everything
// else is UI thread only.
class DownloadService {
public:
	// Told about every finished version install, ok or not, after the library has been updated.
	using InstallObserver = std::function<void(const Modpack& pack, bool ok)>;

	DownloadService(const Runtime& runtime, PackLibrary& library, std::string downloadsFile);

	// The only observer; pack updates use it to carry the player's data over to a new version.
	void SetInstallObserver(InstallObserver observer) { m_observer = std::move(observer); }

	// Finished downloads from earlier sessions, so Discover offers "Install files" again.
	// Entries whose file was deleted are skipped. Call before any worker runs.
	void LoadSaved();
	// Writes every finished download to downloads.json.
	void Save();

	// A copy of one entry's state. False when there is none.
	bool Find(const std::string& key, DownloadState& out) const;
	// A copy of every entry, in key order.
	std::vector<std::pair<std::string, DownloadState>> Snapshot() const;
	// Downloads and installs in progress (the Downloads count in the sidebar).
	int ActiveCount() const;
	// True while a download or install of this pack is running.
	bool IsBusyFor(const std::string& packId) const;

	// Extracts a downloaded file into the version's install folder and, on success, adds the
	// pack to My Library. Used by Discover and the Downloads page.
	void StartInstall(const std::string& url, const Modpack& pack, const std::string& zipPath);

	// Downloads one of a pack's files into downloads/<pack id>/, then extracts it with
	// StartInstall. Discover's one-step Install and pack updates use it.
	void StartDownloadInstall(const Modpack& pack, const PackFile& file);

	// Installs a whole pack (sources whose capabilities say installsPacks): builds the source's
	// install plan, runs it into the version's own folder (installs/<id>@<version>/, or the
	// existing one when reinstalling), then adds the version to My Library.
	void StartPlanInstall(const Modpack& pack);

	// Drops the finished entries of a pack (one version, or with wholePack every version) and
	// saves the list. Used when the pack is uninstalled.
	void Forget(const std::string& packId, bool wholePack, const std::string& versionKey);

private:
	Runtime m_rt;
	PackLibrary& m_library;
	std::string m_file;
	InstallObserver m_observer;
	mutable std::mutex m_mutex;
	std::map<std::string, DownloadState> m_downloads;
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};

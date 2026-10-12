#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Model/Modpack.h"
#include "Services/Downloads/DownloadService.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Library/UninstallService.h"
#include "Services/Play/PlayService.h"

#include <map>
#include <string>

// An update that is installing or copying player data.
struct PackUpdate {
	std::string fromKey;   // the version being updated from
	std::string toKey;     // VersionKey of the version being installed
	std::string toVersion; // its display name
	bool removeOld = false;
	bool copying = false;  // the install finished; player data is being copied
};

// The "Update pack" dialog's state: the newest version is looked up when it opens.
struct UpdateCheck {
	std::string packId; // empty = closed
	bool loading = false;
	std::string error;
	Modpack target;     // the pack at its newest version, once loaded
	bool removeOld = false;
};

// Pack updates. An update installs the source's newest version side by side, carries the
// player's worlds and options over from the version it replaces, then (optionally) uninstalls
// the old version. UI thread only.
class UpdateService {
public:
	UpdateService(const Runtime& runtime, PackLibrary& library, DownloadService& downloads, PlayService& plays,
		UninstallService& uninstaller);

	// The update running for a pack, or null.
	const PackUpdate* Running(const std::string& packId) const;

	// The dialog: look up the newest version of a pack, read what it found, close it.
	void StartCheck(const Modpack& pack);
	UpdateCheck& Check() { return m_check; }
	void CloseCheck() { m_check = UpdateCheck{}; }

	// Installs target (the pack at its newest version) next to libraryPack's active version.
	void Start(const Modpack& libraryPack, const Modpack& target, bool removeOld);

private:
	// When an update's install finishes: copy the player's data into the new version's game
	// folder on a worker, then remove the old version if asked.
	void OnVersionInstalled(const Modpack& pack, bool ok);

	Runtime m_rt;
	PackLibrary& m_library;
	DownloadService& m_downloads;
	PlayService& m_plays;
	UninstallService& m_uninstaller;
	std::map<std::string, PackUpdate> m_updates; // running updates, keyed by pack id
	UpdateCheck m_check;
	int m_checkGeneration = 0;
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};

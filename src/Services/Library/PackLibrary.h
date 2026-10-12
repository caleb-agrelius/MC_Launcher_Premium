#pragma once

#include "Game/InstanceContent.h"
#include "Model/Modpack.h"
#include "Storage/Library.h"

#include <filesystem>
#include <ostream>
#include <string>

// The packs installed on this PC and their versions (My Library), saved to library.json so the
// list survives restarts. UI thread only: workers hand their results over through the UI queue.
// Also knows where each pack's files live: the active version's install folder (the unzipped
// .mrpack, or pack.json + minecraft/) and game folder. Packs not in the library fall back to the
// original single-version layout.
class PackLibrary {
public:
	PackLibrary(std::string file, std::ostream& log);

	Library& Packs() { return m_packs; }
	const Library& Packs() const { return m_packs; }

	bool Contains(const std::string& packId) const { return m_packs.count(packId) > 0; }
	Modpack* Find(const std::string& packId);
	const Modpack* Find(const std::string& packId) const;

	// Writes library.json, noting a failure in the log (the library stays in memory either way).
	void Save();

	// A pack's active installed version, or null when the pack isn't installed.
	const InstalledVersion* ActiveVersionOf(const Modpack& pack) const;
	// The active version's install folder (the extracted pack files).
	std::filesystem::path InstallDir(const Modpack& pack) const;
	// The active version's game folder (mods, config, saves).
	std::filesystem::path GameDir(const Modpack& pack) const;
	// The player's edits to the pack's active version (empty when it isn't installed).
	ContentChanges ContentChangesOf(const Modpack& pack) const;

private:
	// Drops versions whose install folder is gone, and packs left with none.
	void DropMissingVersions();

	std::string m_file;
	std::ostream& m_log;
	Library m_packs;
};

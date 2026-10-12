#pragma once

#include "Model/Modpack.h"

#include <map>
#include <string>
#include <vector>

// Packs whose files have been extracted, keyed by pack id. Saved as JSON in the
// data folder so My Library survives a restart.
using Library = std::map<std::string, Modpack>;

// Missing or unreadable files give an empty library. Packs saved before versions could be
// installed side by side get one version that points at their original folders
// (installs/<id>/, and instances/<id>/ or installs/<id>/minecraft/).
Library LoadPackLibrary(const std::string& path);
bool SavePackLibrary(const std::string& path, const Library& library);

// ---- Installed versions ----

// The folder-safe key of the version a pack describes: its version name, else its
// sourceVersionId, with anything but letters, digits, '.', '-' and '_' replaced.
std::string VersionKey(const Modpack& pack);

const InstalledVersion* FindInstalledVersion(const Modpack& libraryPack, const std::string& key);
// The active version, or null when the pack has none.
const InstalledVersion* ActiveInstalledVersion(const Modpack& libraryPack);

// The folders to install pack's current version into: the existing ones if the library
// already has that version, else new side-by-side ones, installs/<id>@<key>/ plus
// instances/<id>@<key>/ (or the install folder's minecraft/ for whole-pack installs).
InstalledVersion PlanInstalledVersion(const Library& library, const Modpack& pack, bool wholePack);

// Records an installed version (adding the pack to the library if needed, and refreshing
// its name, icon and other details from pack) and makes it the active one.
void AddInstalledVersion(Library& library, const Modpack& pack, const InstalledVersion& version);

// Makes a version active: copies its fields onto the pack's top-level fields.
void SelectInstalledVersion(Modpack& libraryPack, const std::string& key);

// Removes one version from the pack's list (not its files). The newest remaining one
// becomes active if it was. Returns false when no versions are left.
bool RemoveInstalledVersion(Modpack& libraryPack, const std::string& key);

// A finished download, saved so Discover and Downloads remember it after a restart.
struct SavedDownload {
	std::string url; // key in the app's download map
	std::string fileName;
	std::string outPath;
	Modpack pack;
};

// Missing or unreadable files give an empty list.
std::vector<SavedDownload> LoadSavedDownloads(const std::string& path);
bool SaveDownloads(const std::string& path, const std::vector<SavedDownload>& downloads);

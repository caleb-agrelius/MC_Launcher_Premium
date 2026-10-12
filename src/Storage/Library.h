#pragma once

#include "Model/Modpack.h"

#include <map>
#include <string>
#include <vector>

// Packs whose files have been extracted, keyed by pack id. Saved as JSON in the
// data folder so My Library survives a restart.
using Library = std::map<std::string, Modpack>;

// Missing or unreadable files give an empty library.
Library LoadPackLibrary(const std::string& path);
bool SavePackLibrary(const std::string& path, const Library& library);

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

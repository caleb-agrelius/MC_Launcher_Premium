#pragma once

#include "Model/Modpack.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class HttpClient;

// One file a pack install needs. Paths are relative to the instance's minecraft/ folder.
struct PlannedFile {
	std::string name;           // for messages
	std::string url;            // empty => blocked: the user has to download it from manualPageUrl
	std::string relPath;        // where the file goes; for extract files, the folder to unzip into
	std::string sha1, md5;      // lowercase hex; either may be empty
	std::uint64_t size = 0;
	bool extract = false;       // a zip to unpack into relPath instead of saving as a file
	std::string extractPrefix;  // with extract: only unpack entries under this zip folder
	std::string manualPageUrl;  // for blocked files: where to download it by hand
	std::string blockedReason;  // for blocked files: why it can't be downloaded automatically
};

// A zip whose contents are merged into minecraft/ after the files are downloaded.
struct OverrideZip {
	std::string zipPath;
	std::string prefix; // e.g. "overrides/"; empty for the whole zip
};

// Everything needed to install one version of a pack. Each source turns its manifest
// into a plan (see Network/PackSources.h), and RunInstallPlan executes it.
struct InstallPlan {
	PackSource source = PackSource::LocalIndex;
	std::string sourceId;
	std::string sourceVersionId;
	std::string versionName;
	std::string minecraftVersion;
	std::string loader;        // "forge" | "neoforge" | "fabric" | "quilt" | "" (unknown or none)
	std::string loaderVersion;
	std::vector<PlannedFile> files;
	std::vector<OverrideZip> overrides;
	std::vector<std::string> notes; // things the install skipped, shown to the user and saved in pack.json
	std::vector<std::string> clientOnlyFiles; // relPaths the source marks client-only; servers copy them turned off
	int maxConcurrent = 4;
};

struct InstallProgress {
	std::string stage;       // e.g. "Downloading files"
	std::uint64_t done = 0;
	std::uint64_t total = 0;
};

// Installs a plan into installDir:
//   installDir/minecraft/  the game folder (mods/, config/, ...)
//   installDir/pack.json   source, version, Minecraft version, loader, blocked files, notes,
//                          client-only files
// Files already on disk with a matching hash (or size, when there's no hash) are kept,
// so retrying a failed install only downloads what's missing. Blocked files are listed
// in blocked and in pack.json and don't fail the install. Any other failure stops it.
bool RunInstallPlan(HttpClient& http,
					const InstallPlan& plan,
					const std::string& installDir,
					std::function<void(const InstallProgress&)> progress,
					std::vector<PlannedFile>& blocked,
					std::string* err = nullptr,
					const std::atomic<bool>* cancel = nullptr);

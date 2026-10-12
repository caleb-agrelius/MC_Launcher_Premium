#pragma once

#include "Game/GameFiles.h"
#include "Game/Loaders.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

class HttpClient;

// What a modpack needs, read from the modrinth.index.json of an extracted .mrpack.
struct MrpackIndex {
	std::string name;
	std::string versionId;       // the pack's own version
	std::string minecraftVersion;
	LoaderKind loader = LoaderKind::Vanilla;
	std::string loaderVersion;
	std::vector<DownloadJob> files; // client files, paths relative to the instance
};

// extractedDir is installs/<pack id>/ (the unzipped .mrpack).
bool ReadMrpackIndex(const std::filesystem::path& extractedDir, MrpackIndex& out, std::string* err);

// Fills instanceDir (the game directory) with the pack's mods and overrides.
// Files listed in the index are downloaded if missing. overrides/ and
// client-overrides/ are copied only the first time each pack version is
// prepared, so later changes the player makes (options, configs) are kept.
bool PrepareInstance(HttpClient& http,
					 const std::filesystem::path& extractedDir,
					 const std::filesystem::path& instanceDir,
					 const MrpackIndex& index,
					 const ProgressFn& progress,
					 std::string* err,
					 const std::atomic<bool>* cancel);

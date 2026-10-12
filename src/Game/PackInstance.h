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

// A pack installed from a source's install plan (CurseForge, FTB, Technic, ATLauncher):
// installs/<pack id>/pack.json plus a ready-made game folder, installs/<pack id>/minecraft/.
struct InstalledPack {
	std::string minecraftVersion;
	LoaderKind loader = LoaderKind::Vanilla;
	std::string loaderVersion; // normalized for InstallLoader (e.g. "47.2.0", not "forge-47.2.0")
	std::string loaderSource;  // where the loader came from, for messages: "pack.json" or "bin/modpack.jar"
	std::string version;       // the pack's own version (pack.json "version", else "sourceVersionId")
	std::vector<std::string> clientOnlyFiles; // paths under minecraft/ the source marks client-only
};

// True when installDir holds a pack.json (a whole-pack install rather than an .mrpack).
bool IsWholePackInstall(const std::filesystem::path& installDir);

// Reads pack.json. Packs that don't declare a loader (Technic single-zip packs) have it
// detected from the game folder's bin/modpack.jar or bin/version.json, like Prism does.
bool ReadInstalledPack(const std::filesystem::path& installDir, InstalledPack& out, std::string* err);

// "forge" | "neoforge" | "fabric" | "quilt" (any case) -> LoaderKind. Empty -> Vanilla.
bool ParseLoaderName(const std::string& name, LoaderKind& out);

// Strips the forms sources use around a loader version: "forge-47.2.0", "1.20.1-47.2.0",
// "47.2.0-1.20.1" -> "47.2.0".
std::string NormalizeLoaderVersion(const std::string& minecraftVersion, const std::string& version);

// extractedDir is installs/<pack id>/ (the unzipped .mrpack). files lists what the client
// needs, or with forServer what a dedicated server needs (each file's env.client / env.server).
bool ReadMrpackIndex(const std::filesystem::path& extractedDir, MrpackIndex& out, std::string* err,
					 bool forServer = false);

// Checks every file the pack lists against its SHA-1, deletes damaged ones, then runs
// PrepareInstance to download whatever is missing. The player's configs are kept.
bool RepairInstance(HttpClient& http,
					const std::filesystem::path& extractedDir,
					const std::filesystem::path& instanceDir,
					const MrpackIndex& index,
					const ProgressFn& progress,
					std::string* err,
					const std::atomic<bool>* cancel);

// Fills instanceDir (the game directory) with the pack's mods and overrides.
// Files listed in the index are downloaded if missing, unless a "<name>.disabled" copy
// (the player turned it off) is there. overrides/ and
// client-overrides/ are copied only the first time each pack version is
// prepared, so later changes the player makes (options, configs) are kept.
bool PrepareInstance(HttpClient& http,
					 const std::filesystem::path& extractedDir,
					 const std::filesystem::path& instanceDir,
					 const MrpackIndex& index,
					 const ProgressFn& progress,
					 std::string* err,
					 const std::atomic<bool>* cancel);

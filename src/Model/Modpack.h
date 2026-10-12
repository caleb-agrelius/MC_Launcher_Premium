#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Where a pack came from. LocalIndex is resources/index.json (the offline fallback).
enum class PackSource {
	LocalIndex,
	Modrinth,
	CurseForge,
	FTB,
	Technic,
	ATLauncher,
	Custom, // made by the player (My Library > New pack...); a whole-pack install with no online source
};

// Display name ("CurseForge") and the key used in saved JSON ("curseforge").
const char* PackSourceName(PackSource source);
const char* PackSourceKey(PackSource source);
// Unknown keys give LocalIndex.
PackSource PackSourceFromKey(const std::string& key);

struct PackFile {
	std::string name;
	std::string url;
	uint64_t size = 0;
};

// One installed version of a My Library pack. A pack can have several side by side,
// each with its own folders. Paths are relative to the data folder.
struct InstalledVersion {
	std::string key;             // folder-safe id, unique within the pack (from the version name, else sourceVersionId)
	std::string version;         // display name
	std::string sourceVersionId;
	std::string gameVersion;
	std::string loaders;
	std::vector<PackFile> files;          // Modrinth: the version's .mrpack
	std::vector<std::string> optionalMods; // ATLauncher choices for this version
	std::string installDir;      // the unzipped .mrpack, or pack.json + minecraft/ for whole-pack installs
	std::string gameDir;         // the game folder Minecraft runs in
	// What the player added, removed or switched on/off in mods, resource and shader packs,
	// by path in the game folder (see ContentChanges in Game/InstanceContent.h). Not empty
	// means the version is modified.
	std::map<std::string, std::string> contentChanges;
};

struct Modpack {
	std::string id; // unique across sources: Modrinth slug, or "cf-123", "ftb-135", "technic-<slug>", "atl-<name>"
	std::string name;
	std::string description;
	std::string version;
	std::vector<PackFile> files;

	// Extra metadata, filled for online results (empty/zero for local index packs).
	std::string author;
	std::uint64_t downloads = 0;
	std::string gameVersion; // newest supported Minecraft version
	std::string loaders;     // e.g. "fabric", "fabric, quilt"
	std::string iconUrl;     // square pack icon (png/webp)
	std::string bannerUrl;   // small featured gallery image, shown until bannerHiResUrl loads
	std::string bannerHiResUrl; // full-size gallery image, fetched when the pack is selected
	bool bannerLoaded = false;  // true once the full-size banner lookup has finished
	std::uint32_t color = 0; // project accent color as 0xRRGGBB, 0 if unknown
	std::string websiteUrl;  // the pack's page on its source's website

	PackSource source = PackSource::LocalIndex;
	// The source's own ids, used to fetch versions and install. Meaning per source:
	//   CurseForge: project id / file id      FTB: pack id / version id
	//   Technic:    slug / build               ATLauncher: safe name / version
	std::string sourceId;
	std::string sourceVersionId;
	// Technic only: the single-zip download URL, or the Solder API base for Solder packs.
	std::string sourceUrl;
	bool sourceIsSolder = false;
	// ATLauncher only: the optional mods (by name) the player chose to install.
	std::vector<std::string> optionalMods;

	// My Library only: every installed version, and the key of the active one (the one Play
	// uses). The fields above (version, sourceVersionId, gameVersion, loaders, files,
	// optionalMods) mirror the active version.
	std::vector<InstalledVersion> installedVersions;
	std::string activeVersion;

	// Online search results don't include versions; they're fetched when the pack is selected.
	bool filesLoaded = true;
};

// Load a simple index JSON (array of modpacks) from disk.
// Returns an empty vector on error.
std::vector<Modpack> LoadIndex(const std::string& path);

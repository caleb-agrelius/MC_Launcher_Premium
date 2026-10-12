#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

// The content a game or server folder holds: mods, resource packs and shader packs. Every
// kind works the same way: a file (or, for packs, a folder) in one folder, turned off by
// renaming it to "<name>.disabled" so Minecraft skips it.

enum class ContentKind {
	Mods,
	ResourcePacks,
	ShaderPacks,
};

struct ContentKindInfo {
	const char* folder;  // under the game folder, e.g. "mods"
	const char* label;   // e.g. "Mods"
	const char* singular; // e.g. "mod"
	std::vector<std::string> extensions; // lowercase, e.g. ".jar"
	bool folders = false; // unzipped folders count too (resource and shader packs)
};

const ContentKindInfo& GetContentKind(ContentKind kind);

struct ContentFile {
	std::string fileName; // without ".disabled"
	bool enabled = true;
	bool folder = false;
	std::uint64_t size = 0; // 0 for folders
};

// The content in folder (files with one of the extensions, plus folders when allowed), with
// disabled copies, sorted by name ignoring case. A name with both an enabled and a disabled
// copy counts as enabled, since the enabled one is what Minecraft loads.
std::vector<ContentFile> ListContent(const std::filesystem::path& folder, const std::vector<std::string>& extensions,
									 bool folders = false);
std::vector<ContentFile> ListContent(const std::filesystem::path& gameDir, ContentKind kind);

// True when name is one plain file name (no folders, no "..").
bool IsPlainFileName(const std::string& name);

// Renames folder/<fileName> to or from "<fileName>.disabled". Nothing may be using it.
bool SetContentEnabled(const std::filesystem::path& folder, const std::string& fileName, bool enabled, std::string* err);

// Deletes folder/<fileName> and its disabled copy.
bool DeleteContent(const std::filesystem::path& folder, const std::string& fileName, std::string* err);

// Copies source (a file with one of the kind's extensions, or a folder when the kind allows
// folders) into gameDir's folder for that kind. Refuses a name that's already there, on or off.
// fileName gets the name it was added as.
bool AddContent(const std::filesystem::path& gameDir, ContentKind kind, const std::filesystem::path& source,
				std::string* fileName, std::string* err);

// The kind a dropped or picked file belongs to: .jar files are mods, .zip files and folders
// go to preferred when it's resource or shader packs, else resource packs. False for other files.
bool ContentKindForFile(const std::filesystem::path& source, ContentKind preferred, ContentKind* kind);

// ---- Changes the player made to a pack version ----
// By path relative to the game folder ("mods/sodium.jar"): "added", "removed", "disabled"
// (a pack file turned off) or "enabled" (a file the pack ships off, turned on). Saved per
// installed version in library.json; a version with changes shows as modified.
using ContentChanges = std::map<std::string, std::string>;

enum class ContentChange {
	Added,
	Removed,
	Disabled,
	Enabled,
};

// The path a change is recorded under, e.g. ("mods", "sodium.jar") -> "mods/sodium.jar".
std::string ContentPath(ContentKind kind, const std::string& fileName);

// Records one change. Changes that cancel out (removing an added file, turning a file back
// on) leave no entry, so undoing an edit by hand makes the pack unmodified again.
void RecordContentChange(ContentChanges& changes, const std::string& path, ContentChange change);

// E.g. "2 added, 1 removed, 1 turned off"; empty when there are no changes.
std::string DescribeContentChanges(const ContentChanges& changes);

// The pack files the player removed, so installs and repairs don't download them again.
std::set<std::string> RemovedContent(const ContentChanges& changes);

// Puts the player's changes back after the pack's files were installed, repaired or
// updated: removed files are deleted again, and files turned off or on are switched back.
// Files that aren't there are skipped.
void ApplyContentChanges(const std::filesystem::path& gameDir, const ContentChanges& changes);

// Undoes the changes on disk: deletes added files and switches files back to how the pack
// ships them. Removed files have to be downloaded again (Repair files). Stops at the first error.
bool UndoContentChanges(const std::filesystem::path& gameDir, const ContentChanges& changes, std::string* err);

// What a mod jar says about itself: Fabric (fabric.mod.json), Quilt (quilt.mod.json),
// Forge and NeoForge (META-INF/mods.toml, META-INF/neoforge.mods.toml) or old Forge (mcmod.info).
struct ModMetadata {
	std::string id;
	std::string name;
	std::string version;
	std::string description;
	std::vector<std::string> authors;
	std::string loader; // "Fabric", "Quilt", "Forge", "NeoForge"; empty when the jar has no metadata
};

// False when the jar can't be read or has no known metadata file.
bool ReadModMetadata(const std::filesystem::path& jar, ModMetadata& out);

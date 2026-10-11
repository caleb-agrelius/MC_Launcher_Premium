#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PackFile {
	std::string name;
	std::string url;
	uint64_t size = 0;
};

struct Modpack {
	std::string id;
	std::string name;
	std::string description;
	std::string version;
	std::vector<PackFile> files;

	// Extra metadata, filled for Modrinth results (empty/zero for local index packs).
	std::string author;
	std::uint64_t downloads = 0;
	std::string gameVersion; // newest supported Minecraft version
	std::string loaders;     // e.g. "fabric", "fabric, quilt"
	bool fromModrinth = false;
	// Modrinth search results don't include files; they're fetched when the pack is selected.
	bool filesLoaded = true;
};

// Load a simple index JSON (array of modpacks) from disk.
// Returns an empty vector on error.
std::vector<Modpack> LoadIndex(const std::string& path);

#pragma once

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
};

// Load a simple index JSON (array of modpacks) from disk.
// Returns an empty vector on error.
std::vector<Modpack> LoadIndex(const std::string& path);

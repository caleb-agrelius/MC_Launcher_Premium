#pragma once

#include "Model/Modpack.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class HttpClient;

// Client for the public Modrinth API (https://api.modrinth.com/v2). No key needed.

struct ModrinthSearchResult {
	std::vector<Modpack> packs; // filesLoaded == false; use FetchLatestModrinthVersion
	std::uint64_t totalHits = 0;
};

struct ModrinthVersion {
	std::string versionNumber;
	std::string gameVersion;
	std::string loaders;
	std::vector<PackFile> files; // the .mrpack (primary file)
};

// Searches Modrinth modpacks. An empty query lists the most downloaded packs.
bool SearchModrinthModpacks(HttpClient& http,
							const std::string& query,
							int limit,
							ModrinthSearchResult& out,
							std::string* err = nullptr,
							const std::atomic<bool>* cancel = nullptr);

// Fetches the newest release (or newest version if there's no release) of a project.
bool FetchLatestModrinthVersion(HttpClient& http,
								const std::string& projectIdOrSlug,
								ModrinthVersion& out,
								std::string* err = nullptr,
								const std::atomic<bool>* cancel = nullptr);

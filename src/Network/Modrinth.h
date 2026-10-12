#pragma once

#include "Model/Modpack.h"
#include "Network/PackSources.h"

#include <atomic>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

class HttpClient;

// Client for the public Modrinth API (https://api.modrinth.com/v2). No key needed.

struct ModrinthSearchResult {
	std::vector<Modpack> packs; // filesLoaded == false; use FetchLatestModrinthVersion
	std::uint64_t totalHits = 0;
};

struct ModrinthVersion {
	std::string id;          // Modrinth's version id
	std::string versionNumber;
	std::string versionType; // release, beta, alpha
	std::string published;   // YYYY-MM-DD
	std::string gameVersion;
	std::string loaders;
	std::vector<PackFile> files; // the .mrpack (primary file)
};

// Optional search filters. Empty fields don't filter.
struct ModrinthSearchOptions {
	std::string gameVersion; // e.g. "1.20.1"
	std::string loader;      // e.g. "fabric" (a Modrinth category)
	std::string index;       // relevance | downloads | follows | newest | updated; empty = downloads
	                         // for an empty query, relevance otherwise
};

// Searches Modrinth modpacks. An empty query lists the most downloaded packs.
bool SearchModrinthModpacks(HttpClient& http,
							const std::string& query,
							const ModrinthSearchOptions& options,
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

// The Modrinth projects a pack version bundles (its "embedded" dependencies), with names,
// summaries and sides. Files hosted elsewhere aren't dependencies, so they aren't listed.
bool FetchModrinthPackMods(HttpClient& http,
						   const std::string& versionId,
						   std::vector<PackMod>& out,
						   std::string* err = nullptr,
						   const std::atomic<bool>* cancel = nullptr);

// Looks mod files up on Modrinth by SHA-1 and adds the hashes of those whose project
// doesn't run on servers (server_side "unsupported") to clientOnly. Unknown files are left out.
bool FindModrinthClientOnly(HttpClient& http,
							const std::vector<std::string>& sha1s,
							std::set<std::string>& clientOnly,
							std::string* err = nullptr,
							const std::atomic<bool>* cancel = nullptr);

// The project's full description (its "body", in Markdown).
// A version's changelog (Markdown). With an empty versionId, the version whose version
// number is versionNumber (packs installed before versions had ids).
bool FetchModrinthChangelog(HttpClient& http,
							const std::string& projectIdOrSlug,
							const std::string& versionId,
							const std::string& versionNumber,
							std::string& out,
							std::string* err = nullptr,
							const std::atomic<bool>* cancel = nullptr);

bool FetchModrinthDescription(HttpClient& http,
							  const std::string& projectIdOrSlug,
							  std::string& out,
							  std::string* err = nullptr,
							  const std::atomic<bool>* cancel = nullptr);
// Every version of a project that has an .mrpack, newest first.
bool ListModrinthVersions(HttpClient& http,
						  const std::string& projectIdOrSlug,
						  std::vector<ModrinthVersion>& out,
						  std::string* err = nullptr,
						  const std::atomic<bool>* cancel = nullptr);

// Looks up a full-size gallery image for a project's banner (the featured image,
// else the first one). Succeeds with an empty outUrl if the project has no gallery.
bool FetchModrinthBanner(HttpClient& http,
						 const std::string& projectIdOrSlug,
						 std::string& outUrl,
						 std::string* err = nullptr,
						 const std::atomic<bool>* cancel = nullptr);

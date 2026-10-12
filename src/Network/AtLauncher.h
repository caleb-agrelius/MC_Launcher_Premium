#pragma once

#include "Network/PackSources.h"

#include <atomic>
#include <functional>
#include <string>

class HttpClient;

// Client for ATLauncher packs, served as static JSON from its CDN
// (https://download.nodecdn.net/containers/atl/). No key is needed. The pack list is one
// file, fetched once per run and searched locally. A pack's folder name ("safe name") is
// its name with everything but letters and digits removed.

bool SearchAtLauncherModpacks(HttpClient& http,
							  const std::string& query,
							  const SearchFilters& filters,
							  int limit,
							  SourceSearchResult& out,
							  std::string* err = nullptr,
							  const std::atomic<bool>* cancel = nullptr);

// The pack's newest version, plus its loader from the version's Configs.json.
bool FetchAtLauncherVersion(HttpClient& http,
							const Modpack& pack,
							PackVersionInfo& out,
							std::string* err = nullptr,
							const std::atomic<bool>* cancel = nullptr);

// The version's optional client mods, from Configs.json.
bool FetchAtLauncherOptionalMods(HttpClient& http,
								 const Modpack& pack,
								 std::vector<OptionalMod>& out,
								 std::string* err = nullptr,
								 const std::atomic<bool>* cancel = nullptr);

// Reads Configs.json, downloads Configs.zip into workDir, and plans every required client
// mod plus the optional ones named in pack.optionalMods. Other optional mods are skipped
// (and noted).
bool BuildAtLauncherPlan(HttpClient& http,
						 const Modpack& pack,
						 const std::string& workDir,
						 InstallPlan& plan,
						 std::function<void(const std::string&)> status,
						 std::string* err = nullptr,
						 const std::atomic<bool>* cancel = nullptr);

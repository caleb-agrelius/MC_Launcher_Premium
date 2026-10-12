#pragma once

#include "Network/PackSources.h"

#include <atomic>
#include <functional>
#include <string>

class HttpClient;

// Client for the Technic Platform API (https://api.technicpack.net/). No key is needed, but
// every request must send a "build" value; we send our own launcher identifier.
// A pack is either a single zip (unpacked as-is) or served by a Solder server, whose builds
// list mod zips that are each unpacked into the game folder.
// The API has no filters or sorts. When a Minecraft version filter or the "Most installed"
// sort is chosen, each result's details are fetched (4 at a time) and filtered/sorted here.

bool SearchTechnicModpacks(HttpClient& http,
						   const std::string& query,
						   const SearchFilters& filters,
						   int limit,
						   SourceSearchResult& out,
						   std::string* err = nullptr,
						   const std::atomic<bool>* cancel = nullptr);

// Pack details (author, description, Minecraft version) plus the version to install:
// the Solder server's recommended build, or the single zip's version.
bool FetchTechnicVersion(HttpClient& http,
						 const Modpack& pack,
						 PackVersionInfo& out,
						 std::string* err = nullptr,
						 const std::atomic<bool>* cancel = nullptr);

// A Solder build's mods. Single-zip packs succeed with an empty list.
bool FetchTechnicPackMods(HttpClient& http,
						  const Modpack& pack,
						  std::vector<PackMod>& out,
						  std::string* err = nullptr,
						  const std::atomic<bool>* cancel = nullptr);

bool BuildTechnicPlan(HttpClient& http,
					  const Modpack& pack,
					  InstallPlan& plan,
					  std::function<void(const std::string&)> status,
					  std::string* err = nullptr,
					  const std::atomic<bool>* cancel = nullptr);

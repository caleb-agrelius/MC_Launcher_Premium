#pragma once

#include "Network/PackSources.h"

#include <atomic>
#include <functional>
#include <string>

class HttpClient;

// Client for the public FTB modpack API (https://api.feed-the-beast.com/v1/modpacks/public).
// No key is needed. The API has no search, so the first search fetches every pack's details
// (about a hundred) and keeps them in memory; searches and filters then run locally.

bool SearchFtbModpacks(HttpClient& http,
					   const std::string& query,
					   const SearchFilters& filters,
					   int limit,
					   SourceSearchResult& out,
					   std::string* err = nullptr,
					   const std::atomic<bool>* cancel = nullptr);

// Every version of a pack (pack.sourceId), newest first.
bool ListFtbVersions(HttpClient& http,
		const Modpack& pack,
		std::vector<PackVersionInfo>& out,
		std::string* err = nullptr,
		const std::atomic<bool>* cancel = nullptr);

// The newest release of a pack (pack.sourceId), else its newest non-archived version.
bool FetchFtbVersion(HttpClient& http,
					 const Modpack& pack,
					 PackVersionInfo& out,
					 std::string* err = nullptr,
					 const std::atomic<bool>* cancel = nullptr);

// Reads the version's file list. Files hosted on CurseForge are resolved through the
// CurseForge API when a key is configured, else listed as blocked.
bool BuildFtbPlan(HttpClient& http,
				  const Modpack& pack,
				  InstallPlan& plan,
				  std::function<void(const std::string&)> status,
				  std::string* err = nullptr,
				  const std::atomic<bool>* cancel = nullptr);

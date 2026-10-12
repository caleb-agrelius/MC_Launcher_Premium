#pragma once

#include "Network/PackSources.h"

#include <atomic>
#include <functional>
#include <string>

class IHttpClient;

// Client for the public FTB modpack API (https://api.feed-the-beast.com/v1/modpacks/public).
// No key is needed. The API has no search, so the first search fetches every pack's details
// (about a hundred) and keeps them in memory; searches and filters then run locally.

bool SearchFtbModpacks(IHttpClient& http,
					   const std::string& query,
					   const SearchFilters& filters,
					   int limit,
					   SourceSearchResult& out,
					   std::string* err = nullptr,
					   const std::atomic<bool>* cancel = nullptr);

// Every version of a pack (pack.sourceId), newest first.
bool ListFtbVersions(IHttpClient& http,
		const Modpack& pack,
		std::vector<PackVersionInfo>& out,
		std::string* err = nullptr,
		const std::atomic<bool>* cancel = nullptr);

// The newest release of a pack (pack.sourceId), else its newest non-archived version.
bool FetchFtbVersion(IHttpClient& http,
					 const Modpack& pack,
					 PackVersionInfo& out,
					 std::string* err = nullptr,
					 const std::atomic<bool>* cancel = nullptr);

// The mods in the version's file list (type "mod"), named after their files.
bool FetchFtbPackMods(IHttpClient& http,
					  const Modpack& pack,
					  std::vector<PackMod>& out,
					  std::string* err = nullptr,
					  const std::atomic<bool>* cancel = nullptr);

// The pack's full description (Markdown).
// The changelog (Markdown) of the pack's version (pack.sourceVersionId).
bool FetchFtbChangelog(IHttpClient& http,
					   const Modpack& pack,
					   std::string& out,
					   std::string* err = nullptr,
					   const std::atomic<bool>* cancel = nullptr);

bool FetchFtbDescription(IHttpClient& http,
						 const Modpack& pack,
						 std::string& out,
						 std::string* err = nullptr,
						 const std::atomic<bool>* cancel = nullptr);

// Reads the version's file list. Files hosted on CurseForge are resolved through the
// CurseForge API when a key is configured, else listed as blocked.
bool BuildFtbPlan(IHttpClient& http,
				  const Modpack& pack,
				  InstallPlan& plan,
				  std::function<void(const std::string&)> status,
				  std::string* err = nullptr,
				  const std::atomic<bool>* cancel = nullptr);

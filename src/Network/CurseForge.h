#pragma once

#include "Network/ModSearch.h"
#include "Network/PackSources.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

class IHttpClient;

// Client for the CurseForge API (https://api.curseforge.com/v1). Every request needs an
// API key; see CurseForgeApiKey(). Files whose authors turned off third-party downloads
// come back without a download URL. Those are looked up on Modrinth by SHA-1, and if that
// fails they're returned as blocked (url empty) with a link to download them by hand.

// The key saved in Settings (see SetCurseForgeApiKey), else the MCLP_CURSEFORGE_API_KEY
// environment variable. Empty if neither is set. Safe to call from any thread.
std::string CurseForgeApiKey();
// Sets the key from Settings (surrounding spaces are trimmed; empty means "use the
// environment variable"). Call at startup and whenever the setting changes.
void SetCurseForgeApiKey(const std::string& key);
// The MCLP_CURSEFORGE_API_KEY environment variable, or empty if it isn't set.
std::string CurseForgeEnvApiKey();

bool SearchCurseForgeModpacks(IHttpClient& http,
							  const std::string& query,
							  const SearchFilters& filters,
							  int limit,
							  SourceSearchResult& out,
							  std::string* err = nullptr,
							  const std::atomic<bool>* cancel = nullptr);

// Every file of a modpack project (pack.sourceId), newest first, read page by page
// (up to 500 files).
bool ListCurseForgeVersions(IHttpClient& http,
		const Modpack& pack,
		std::vector<PackVersionInfo>& out,
		std::string* err = nullptr,
		const std::atomic<bool>* cancel = nullptr);

// The newest release file of a modpack project (pack.sourceId).
bool FetchCurseForgeVersion(IHttpClient& http,
							const Modpack& pack,
							PackVersionInfo& out,
							std::string* err = nullptr,
							const std::atomic<bool>* cancel = nullptr);

// Downloads the pack zip (pack.sourceVersionId) into workDir, reads its manifest.json,
// and resolves every listed file.
bool BuildCurseForgePlan(IHttpClient& http,
						 const Modpack& pack,
						 const std::string& workDir,
						 InstallPlan& plan,
						 std::function<void(const std::string&)> status,
						 std::string* err = nullptr,
						 const std::atomic<bool>* cancel = nullptr);

// The projects listed in the pack zip's manifest (downloaded into workDir, like
// BuildCurseForgePlan, so a later install reuses it), with names and summaries.
bool FetchCurseForgePackMods(IHttpClient& http,
							 const Modpack& pack,
							 const std::string& workDir,
							 std::vector<PackMod>& out,
							 std::string* err = nullptr,
							 const std::atomic<bool>* cancel = nullptr);

// The project's full description (HTML), from /mods/{id}/description.
// The changelog (HTML) of the pack's file (pack.sourceVersionId).
bool FetchCurseForgeChangelog(IHttpClient& http,
							  const Modpack& pack,
							  std::string& out,
							  std::string* err = nullptr,
							  const std::atomic<bool>* cancel = nullptr);

bool FetchCurseForgeDescription(IHttpClient& http,
								const Modpack& pack,
								std::string& out,
								std::string* err = nullptr,
								const std::atomic<bool>* cancel = nullptr);

// A pack's manifest.json, the parts the installer uses.
struct CurseForgeManifest {
	std::string name;
	std::string version;
	std::string minecraftVersion;
	std::string loader;        // e.g. "forge" from "forge-47.2.0"
	std::string loaderVersion; // e.g. "47.2.0"
	std::string overrides = "overrides";
	struct File {
		std::int64_t projectId = 0;
		std::int64_t fileId = 0;
		bool required = true;
	};
	std::vector<File> files;
};

bool ParseCurseForgeManifest(const std::string& text, CurseForgeManifest& out, std::string* err = nullptr);

// One CurseForge file to resolve. relPath empty = "<folder for the project's class>/<fileName>".
struct CurseForgeFileRef {
	std::int64_t projectId = 0;
	std::int64_t fileId = 0;
	std::string relPath;
};

// Resolves files to download URLs and hashes (POST /mods/files and /mods), then tries
// Modrinth for files without a URL. out has one PlannedFile per ref, in order.
// Also used by FTB for its CurseForge-hosted files.
bool ResolveCurseForgeFiles(IHttpClient& http,
							const std::vector<CurseForgeFileRef>& refs,
							std::vector<PlannedFile>& out,
							std::string* err = nullptr,
							const std::atomic<bool>* cancel = nullptr);

// Fills in URLs for blocked files (url empty, sha1 set) from Modrinth's hash lookup.
// Returns how many were found. Failures leave the files blocked.
int FillFromModrinthByHash(IHttpClient& http, std::vector<PlannedFile>& files, const std::atomic<bool>* cancel = nullptr);

// Mods, resource packs and shaders for Manage > Content > Browse (see Network/ModSearch.h, which
// calls these when a search's source is CurseForge). Project ids are "cf-" and the CurseForge id,
// so they never collide with Modrinth's. All of them need an API key.

// /mods/search for the search's kind (class 6, 12 or 6552), Minecraft version and loader. With no
// text the most downloaded come first.
bool SearchCurseForgeContent(IHttpClient& http, const ContentSearch& search, int limit, std::vector<ContentProject>& out,
							 std::string* err = nullptr, const std::atomic<bool>* cancel = nullptr);

// The newest release (else the newest file) of projectId that fits search, from /mods/{id}/files, with the
// projects it requires (relationType 3). A file whose author blocks third-party downloads is looked up on
// Modrinth by SHA-1; if that fails the error says where to download it by hand.
bool FindCurseForgeContentDownload(IHttpClient& http, const ContentSearch& search, const std::string& projectId,
								   ContentDownload& out, std::string* err = nullptr, const std::atomic<bool>* cancel = nullptr);

// The projects the files in folder belong to, found by their CurseForge fingerprints
// (POST /fingerprints/432). Files CurseForge doesn't know are left out.
bool FindCurseForgeInstalledProjects(IHttpClient& http, const std::filesystem::path& folder, std::set<std::string>& projectIds,
									 std::string* err = nullptr, const std::atomic<bool>* cancel = nullptr);

#pragma once

#include "Network/PackSources.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class HttpClient;

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

bool SearchCurseForgeModpacks(HttpClient& http,
							  const std::string& query,
							  const SearchFilters& filters,
							  int limit,
							  SourceSearchResult& out,
							  std::string* err = nullptr,
							  const std::atomic<bool>* cancel = nullptr);

// The newest release file of a modpack project (pack.sourceId).
bool FetchCurseForgeVersion(HttpClient& http,
							const Modpack& pack,
							PackVersionInfo& out,
							std::string* err = nullptr,
							const std::atomic<bool>* cancel = nullptr);

// Downloads the pack zip (pack.sourceVersionId) into workDir, reads its manifest.json,
// and resolves every listed file.
bool BuildCurseForgePlan(HttpClient& http,
						 const Modpack& pack,
						 const std::string& workDir,
						 InstallPlan& plan,
						 std::function<void(const std::string&)> status,
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
bool ResolveCurseForgeFiles(HttpClient& http,
							const std::vector<CurseForgeFileRef>& refs,
							std::vector<PlannedFile>& out,
							std::string* err = nullptr,
							const std::atomic<bool>* cancel = nullptr);

// Fills in URLs for blocked files (url empty, sha1 set) from Modrinth's hash lookup.
// Returns how many were found. Failures leave the files blocked.
int FillFromModrinthByHash(HttpClient& http, std::vector<PlannedFile>& files, const std::atomic<bool>* cancel = nullptr);

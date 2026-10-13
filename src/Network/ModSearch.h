#pragma once

#include "Game/InstanceContent.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

class IHttpClient;

// Mods, resource packs and shader packs from Modrinth or CurseForge, for adding to an installed
// or new pack (Manage > Content > Browse).

// Where the content comes from. CurseForge needs an API key (see Network/CurseForge.h).
enum class ContentSource {
	Modrinth,
	CurseForge,
};

const char* ContentSourceName(ContentSource source); // "Modrinth", "CurseForge"

// What to look for, and what it has to work with.
struct ContentSearch {
	ContentKind kind = ContentKind::Mods;
	std::string query;       // empty lists the most downloaded
	std::string gameVersion; // the pack's Minecraft version; empty doesn't filter
	std::string loader;      // "fabric", "quilt", "forge", "neoforge"; mods only; empty doesn't filter
	ContentSource source = ContentSource::Modrinth;
};

// One search result.
struct ContentProject {
	std::string id;          // Modrinth project id, or "cf-" and the CurseForge id
	std::string name;
	std::string summary;
	std::string author;
	std::string iconUrl;
	std::string pageUrl;
	std::uint64_t downloads = 0;
};

// The file of a project that fits the search's Minecraft version and loader.
struct ContentDownload {
	std::string projectId;
	std::string fileName;
	std::string url;
	std::string sha1;        // lowercase hex; may be empty
	std::uint64_t size = 0;
	std::vector<std::string> requiredProjects; // project ids it needs
};

bool SearchContent(IHttpClient& http, const ContentSearch& search, int limit, std::vector<ContentProject>& out,
				   std::string* err = nullptr, const std::atomic<bool>* cancel = nullptr);

// The newest release (else the newest version) of projectId that fits search.
bool FindContentDownload(IHttpClient& http, const ContentSearch& search, const std::string& projectId,
						 ContentDownload& out, std::string* err = nullptr, const std::atomic<bool>* cancel = nullptr);

// The projects the files in folder come from: looked up by SHA-1 on Modrinth, or by fingerprint on
// CurseForge (files the source doesn't know are left out).
bool FindInstalledProjects(IHttpClient& http, const std::filesystem::path& folder, std::set<std::string>& projectIds,
						   std::string* err = nullptr, const std::atomic<bool>* cancel = nullptr,
						   ContentSource source = ContentSource::Modrinth);

struct AddedContent {
	ContentKind kind = ContentKind::Mods;
	std::string fileName;
	std::string projectId;
};

// Downloads projectId's file (from search.source) into gameDir's folder for search.kind, then the mods it requires
// that gameDir/mods doesn't have yet (found with FindInstalledProjects), and theirs. Files
// already there by name are skipped. added lists what was downloaded; notes say which
// required mods couldn't be added (no file for this Minecraft version and loader).
bool InstallContent(IHttpClient& http, const ContentSearch& search, const std::string& projectId,
					const std::filesystem::path& gameDir, std::vector<AddedContent>& added, std::vector<std::string>& notes,
					std::string* err = nullptr, const std::atomic<bool>* cancel = nullptr);

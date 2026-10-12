// Opt-in checks against the real pack source APIs. They need the internet, so CTest
// doesn't run them: build the live_checks target and run it from the repository root.
// CurseForge is checked only when an API key is set (MCLP_CURSEFORGE_API_KEY).

#include "Network/CurseForge.h"
#include "Network/HttpClient.h"
#include "Network/Modrinth.h"
#include "Network/PackSources.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace fs = std::filesystem;

static int g_failures = 0;

static void Report(const std::string& what, bool ok, const std::string& detail)
{
	std::cout << (ok ? "PASS  " : "FAIL  ") << what << ": " << detail << std::endl;
	if (!ok)
		++g_failures;
}

// Searches a source, loads the first pack whose version loads (a Solder pack for Technic),
// and fetches its mod list.
static void CheckSource(HttpClient& http, PackSource source, const std::string& query, const fs::path& workRoot)
{
	const std::string name = PackSourceName(source);
	SourceSearchResult result;
	std::string error;
	if (!SearchPacks(http, source, query, {}, 15, result, &error)) {
		Report(name + " search", false, error);
		return;
	}
	for (Modpack pack : result.packs) {
		PackVersionInfo version;
		if (!FetchPackVersion(http, pack, version, &error))
			continue;
		ApplyPackVersion(pack, version);
		if (source == PackSource::Technic && !pack.sourceIsSolder)
			continue;
		PackModList mods;
		const bool ok = FetchPackMods(http, pack, (workRoot / pack.id).string(), mods, &error);
		std::string detail = pack.name + " " + pack.version + ": ";
		if (!ok)
			detail += error;
		else
			detail += std::to_string(mods.mods.size()) + " mod(s)" + (mods.mods.empty() ? "" : ", first " + mods.mods.front().name);
		Report(name + " mod list", ok && !mods.mods.empty(), detail);
		std::string description;
		const bool described = FetchPackDescription(http, pack, description, &error);
		Report(name + " description", described && !description.empty() && description.find('<') == std::string::npos &&
			description.find("](") == std::string::npos,
			described ? std::to_string(description.size()) + " characters: " + description.substr(0, 60) : error);
		return;
	}
	Report(name + " mod list", false, "no pack in the first results had a usable version");
}

// The newest file hash of a Modrinth project.
static std::string LatestFileSha1(HttpClient& http, const std::string& project)
{
	std::string body;
	if (!http.Get("https://api.modrinth.com/v2/project/" + project + "/version", body))
		return {};
	try {
		return nlohmann::json::parse(body).at(0).at("files").at(0).at("hashes").at("sha1").get<std::string>();
	} catch (...) {
		return {};
	}
}

// Sodium doesn't run on servers; Lithium does.
static void CheckClientOnlyLookup(HttpClient& http)
{
	const std::string sodium = LatestFileSha1(http, "sodium");
	const std::string lithium = LatestFileSha1(http, "lithium");
	std::set<std::string> clientOnly;
	std::string error;
	const bool ok = !sodium.empty() && !lithium.empty() && FindModrinthClientOnly(http, { sodium, lithium }, clientOnly, &error);
	Report("Modrinth client-only lookup", ok && clientOnly.count(sodium) && !clientOnly.count(lithium),
		ok ? std::to_string(clientOnly.size()) + " of 2 flagged" : error);
}

int main()
{
	HttpClient http;
	const fs::path workRoot = fs::absolute("tests/out/live");
	fs::create_directories(workRoot);
	CheckSource(http, PackSource::Modrinth, "fabulously optimized", workRoot);
	CheckSource(http, PackSource::FTB, "", workRoot);
	CheckSource(http, PackSource::ATLauncher, "", workRoot);
	CheckSource(http, PackSource::Technic, "tekkit", workRoot);
	CheckClientOnlyLookup(http);
	if (!CurseForgeApiKey().empty())
		CheckSource(http, PackSource::CurseForge, "", workRoot);
	else
		std::cout << "SKIP  CurseForge: no API key\n";
	std::cout << (g_failures == 0 ? "All live checks passed\n" : std::to_string(g_failures) + " live check(s) failed\n");
	return g_failures == 0 ? 0 : 1;
}

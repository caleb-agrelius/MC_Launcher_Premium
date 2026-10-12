// Opt-in checks against the real pack source APIs. They need the internet, so CTest
// doesn't run them: build the live_checks target and run it from the repository root.
// CurseForge is checked only when an API key is set (MCLP_CURSEFORGE_API_KEY).

#include "Game/PackCreator.h"
#include "Network/CurseForge.h"
#include "Network/HttpClient.h"
#include "Network/ModSearch.h"
#include "Network/Modrinth.h"
#include "Network/PackSources.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
		if (GetSourceCapabilities(source).changelogs) {
			std::string changelog;
			const bool logged = FetchPackChangelog(http, pack, changelog, &error);
			Report(name + " changelog", logged && changelog.find('<') == std::string::npos,
				logged ? std::to_string(changelog.size()) + " characters: " + changelog.substr(0, 60) : error);
		}
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

// New packs: Minecraft and loader versions, then Modrinth content with its dependencies.
static void CheckPackCreation(HttpClient& http, const fs::path& workRoot)
{
	std::string error;
	std::vector<std::string> releases;
	const bool releasesOk = FetchMinecraftReleases(http, releases, &error);
	Report("Minecraft releases", releasesOk && !releases.empty() &&
		std::find(releases.begin(), releases.end(), "1.20.1") != releases.end(),
		releasesOk ? std::to_string(releases.size()) + " releases, newest " + (releases.empty() ? "" : releases.front()) : error);
	for (const auto& [loader, minecraft] : std::vector<std::pair<std::string, std::string>>{
			{ "fabric", "1.20.1" }, { "quilt", "1.20.1" }, { "forge", "1.20.1" }, { "neoforge", "1.21.1" } }) {
		std::vector<std::string> versions;
		const bool ok = FetchLoaderVersions(http, loader, minecraft, versions, &error);
		Report(loader + " versions for " + minecraft, ok && !versions.empty(),
			ok ? std::to_string(versions.size()) + " versions, first " + (versions.empty() ? "" : versions.front()) : error);
	}
	std::vector<std::string> none;
	Report("fabric versions for an unknown Minecraft", FetchLoaderVersions(http, "fabric", "0.0.1", none, &error) && none.empty(),
		error);

	ContentSearch search;
	search.kind = ContentKind::Mods;
	search.query = "sodium extra";
	search.gameVersion = "1.20.1";
	search.loader = "fabric";
	std::vector<ContentProject> projects;
	const bool searched = SearchContent(http, search, 10, projects, &error);
	Report("Modrinth mod search", searched && !projects.empty(),
		searched ? std::to_string(projects.size()) + " results, first " + (projects.empty() ? "" : projects.front().name) : error);
	search.kind = ContentKind::ShaderPacks;
	search.query.clear();
	std::vector<ContentProject> shaders;
	Report("Modrinth shader search", SearchContent(http, search, 5, shaders, &error) && !shaders.empty(), error);

	// Sodium Extra needs Sodium: installing it into an empty pack adds both.
	const fs::path game = workRoot / "custom-pack" / "minecraft";
	fs::remove_all(game.parent_path());
	search.kind = ContentKind::Mods;
	std::vector<AddedContent> added;
	std::vector<std::string> notes;
	const bool installed = InstallContent(http, search, "sodium-extra", game, added, notes, &error);
	std::string names;
	for (const AddedContent& file : added)
		names += file.fileName + " ";
	Report("Install Sodium Extra with its dependencies", installed && added.size() >= 2 &&
		names.find("sodium-fabric") != std::string::npos, installed ? names : error);
	std::set<std::string> projectIds;
	const bool found = FindInstalledProjects(http, game / "mods", projectIds, &error);
	Report("Installed projects by hash", found && projectIds.size() == 3,
		found ? std::to_string(projectIds.size()) + " projects" : error);
	// Again: everything is there already, so nothing is downloaded.
	const bool again = InstallContent(http, search, "sodium-extra", game, added, notes, &error);
	Report("Installing it again adds nothing", again && added.empty(), again ? std::to_string(added.size()) + " added" : error);
	ContentDownload download;
	Report("A mod with no build for the pack is refused",
		!FindContentDownload(http, { ContentKind::Mods, "", "1.20.1", "forge" }, "sodium", download, &error), error);
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
	CheckPackCreation(http, workRoot);
	if (!CurseForgeApiKey().empty())
		CheckSource(http, PackSource::CurseForge, "", workRoot);
	else
		std::cout << "SKIP  CurseForge: no API key\n";
	std::cout << (g_failures == 0 ? "All live checks passed\n" : std::to_string(g_failures) + " live check(s) failed\n");
	return g_failures == 0 ? 0 : 1;
}

// Opt-in checks against the real pack source APIs. They need the internet, so CTest
// doesn't run them: build the live_checks target and run it from the repository root.
// CurseForge is checked only when an API key is set (MCLP_CURSEFORGE_API_KEY).
// "live_checks --tls" makes one HTTPS request and stops.

#include "Game/PackCreator.h"
#include "Game/ServerSetup.h"
#include "Network/CurseForge.h"
#include "Network/HttpClient.h"
#include "Network/ModSearch.h"
#include "Network/Modrinth.h"
#include "Network/PackSources.h"
#include "Util/Process.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <thread>

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
static void CheckSource(IHttpClient& http, PackSource source, const std::string& query, const fs::path& workRoot)
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
static std::string LatestFileSha1(IHttpClient& http, const std::string& project)
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
static void CheckClientOnlyLookup(IHttpClient& http)
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
static void CheckPackCreation(IHttpClient& http, const fs::path& workRoot)
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

// CurseForge as a source in Manage > Content > Browse (needs an API key): mod, resource pack and shader
// search, installing a mod with what it requires, finding it again by fingerprint, and a mod with no
// build for the pack. Project ids come from the search, so nothing here depends on a particular mod.
static void CheckCurseForgeContent(IHttpClient& http, const fs::path& workRoot)
{
	std::string error;
	ContentSearch search;
	search.kind = ContentKind::Mods;
	search.query = "sodium";
	search.gameVersion = "1.20.1";
	search.loader = "fabric";
	search.source = ContentSource::CurseForge;
	std::vector<ContentProject> projects;
	const bool searched = SearchContent(http, search, 10, projects, &error);
	Report("CurseForge mod search", searched && !projects.empty() && projects.front().id.rfind("cf-", 0) == 0,
		searched ? std::to_string(projects.size()) + " results, first " + (projects.empty() ? "" : projects.front().name) : error);
	search.query.clear();
	const bool popular = SearchContent(http, search, 10, projects, &error);
	Report("CurseForge most downloaded mods", popular && projects.size() >= 5, popular ? std::to_string(projects.size()) + " results" : error);
	for (const ContentKind kind : { ContentKind::ResourcePacks, ContentKind::ShaderPacks }) {
		ContentSearch other = search;
		other.kind = kind;
		std::vector<ContentProject> found;
		const bool ok = SearchContent(http, other, 5, found, &error);
		Report(std::string("CurseForge ") + GetContentKind(kind).label + " search", ok && !found.empty(),
			ok ? std::to_string(found.size()) + " results" : error);
	}
	if (projects.empty())
		return;

	// Install the first result into an empty pack, with whatever it requires. A mod whose author blocks
	// downloads can't be added, so the first few results are tried.
	const fs::path game = workRoot / "curseforge-content" / "minecraft";
	fs::remove_all(game.parent_path());
	std::vector<AddedContent> added;
	std::vector<std::string> notes;
	std::string installedId;
	for (std::size_t i = 0; i < std::min<std::size_t>(projects.size(), 5) && installedId.empty(); ++i) {
		if (InstallContent(http, search, projects[i].id, game, added, notes, &error))
			installedId = projects[i].id;
	}
	std::string names;
	for (const AddedContent& file : added)
		names += file.fileName + " ";
	Report("CurseForge install with its dependencies", !installedId.empty() && !added.empty(), installedId.empty() ? error : names);
	if (installedId.empty())
		return;
	std::set<std::string> projectIds;
	const bool found = FindInstalledProjects(http, game / "mods", projectIds, &error, nullptr, ContentSource::CurseForge);
	Report("CurseForge installed projects by fingerprint", found && projectIds.count(installedId) == 1,
		found ? std::to_string(projectIds.size()) + " projects" : error);
	const bool again = InstallContent(http, search, installedId, game, added, notes, &error);
	Report("Installing it again adds nothing", again && added.empty(), again ? std::to_string(added.size()) + " added" : error);
	ContentDownload download;
	search.gameVersion = "1.0";
	Report("A mod with no build for the pack is refused", !FindContentDownload(http, search, installedId, download, &error), error);
}

// Modrinth categories: the list loads, and a category narrows a search.
static void CheckCategories(HttpClient& http)
{
	std::vector<ModrinthCategory> categories;
	std::string error;
	if (!FetchModrinthCategories(http, categories, &error)) {
		Report("Modrinth categories", false, error);
		return;
	}
	std::string names;
	bool hasAdventure = false;
	for (const ModrinthCategory& category : categories) {
		names += (names.empty() ? "" : ", ") + category.label;
		hasAdventure = hasAdventure || category.key == "adventure";
	}
	Report("Modrinth categories", hasAdventure, std::to_string(categories.size()) + ": " + names);
	SearchFilters any, adventure;
	adventure.category = "adventure";
	SourceSearchResult all, filtered;
	const bool ok = SearchPacks(http, PackSource::Modrinth, "", any, 10, all, &error) &&
		SearchPacks(http, PackSource::Modrinth, "", adventure, 10, filtered, &error);
	Report("Modrinth category search", ok && filtered.totalHits > 0 && filtered.totalHits < all.totalHits,
		std::to_string(filtered.totalHits) + " adventure of " + std::to_string(all.totalHits) + ", first " +
		(filtered.packs.empty() ? std::string("-") : filtered.packs.front().name) + " " + error);
}

// Discover paging: page 2 (offset = page size) continues page 1 with no repeats.
static void CheckPaging(IHttpClient& http, PackSource source, const std::string& query)
{
	const std::string name = std::string(PackSourceName(source)) + " paging";
	constexpr int kPage = 10;
	SearchFilters filters;
	SourceSearchResult first, second;
	std::string error;
	if (!SearchPacks(http, source, query, filters, kPage, first, &error)) {
		Report(name, false, "page 1: " + error);
		return;
	}
	filters.offset = static_cast<int>(first.packs.size());
	if (!SearchPacks(http, source, query, filters, kPage, second, &error)) {
		Report(name, false, "page 2: " + error);
		return;
	}
	std::set<std::string> ids;
	for (const Modpack& pack : first.packs)
		ids.insert(pack.id);
	std::size_t repeats = 0;
	for (const Modpack& pack : second.packs)
		repeats += ids.count(pack.id);
	const bool ok = first.packs.size() == kPage && !second.packs.empty() && repeats == 0 && first.totalHits > kPage;
	Report(name, ok, std::to_string(first.packs.size()) + " + " + std::to_string(second.packs.size()) + " of " +
		std::to_string(first.totalHits) + ", " + std::to_string(repeats) + " repeated; page 2 starts with " +
		(second.packs.empty() ? std::string("-") : second.packs.front().name));
}

// An old Forge server built from the Forge universal jar a pack ships, then started until it
// reports "Done". jarPath is where the pack keeps it: Technic's bin/modpack.jar, or an
// ATLauncher jar mod in jarmods/. forge is the Forge maven version, e.g. "1.7.10-10.13.4.1614-1.7.10".
static void CheckLegacyForgeServer(IHttpClient& http, const fs::path& workRoot, const std::string& minecraft,
	const std::string& forgeVersion, const std::string& source, const fs::path& jarPath)
{
	const std::string what = "Old Forge server (" + minecraft + ", " + source + ")";
	const fs::path root = workRoot / ("legacy-forge-" + minecraft);
	const fs::path install = root / "install";
	ServerSetupRequest request;
	request.gameRoot = root / "game";
	request.extractedDir = install;
	request.serverDir = root / "server";
	std::error_code ec;
	fs::remove_all(install, ec);
	fs::remove_all(request.serverDir, ec);
	fs::create_directories(install / "minecraft" / "mods", ec);
	std::ofstream(install / "pack.json") << nlohmann::json{ { "source", source }, { "minecraft", minecraft },
		{ "version", "live-check" } }.dump();
	DownloadJob forge;
	forge.url = "https://maven.minecraftforge.net/net/minecraftforge/forge/" + forgeVersion + "/forge-" + forgeVersion +
		"-universal.jar";
	forge.path = install / "minecraft" / jarPath;
	std::string error;
	if (!DownloadAll(http, { forge }, "Downloading Forge", nullptr, &error, nullptr)) {
		Report(what, false, "couldn't download the Forge jar: " + error);
		return;
	}
	ServerInfo info;
	if (!SetupServer(http, request, info, nullptr, &error, nullptr)) {
		Report(what, false, "setup failed: " + error);
		return;
	}
	std::ofstream(request.serverDir / "eula.txt") << "eula=true\n";
	ServerProcess server;
	if (!server.Start(ServerCommand(info, 1024), request.serverDir, request.serverDir / "logs" / "live-check.log", &error)) {
		Report(what, false, "couldn't start the server: " + error);
		return;
	}
	bool done = false;
	for (int waited = 0; waited < 240 && !done && server.Running(); ++waited) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		for (const std::string& line : server.Lines())
			done = done || line.find("Done (") != std::string::npos;
	}
	std::string last;
	if (!done) {
		const std::vector<std::string> lines = server.Lines();
		for (std::size_t i = lines.size() > 8 ? lines.size() - 8 : 0; i < lines.size(); ++i)
			last += "\n    " + lines[i];
	}
	server.Send("stop");
	if (!server.WaitForExit(60000))
		server.Kill();
	Report(what, done, done ? info.loader + " " + info.loaderVersion + " with Java " + std::to_string(info.javaMajor) +
		" started and stopped (" + info.launchArgs[info.launchArgs.size() - 2] + ")" : "the server didn't finish starting:" + last);
}

// The quick check CI runs on each platform: one HTTPS request, which proves libcurl finds the system's
// certificates and can reach a real server. (Everything else here takes minutes.)
static int CheckTls(HttpClient& http)
{
	std::string body, error;
	const bool ok = http.Get("https://api.modrinth.com/", body, &error);
	Report("HTTPS request to api.modrinth.com", ok && body.find("modrinth") != std::string::npos,
		ok ? std::to_string(body.size()) + " bytes" : error);
	return g_failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
	HttpClient http;
	if (argc > 1 && std::string(argv[1]) == "--tls")
		return CheckTls(http);
	const fs::path workRoot = fs::absolute("tests/out/live");
	fs::create_directories(workRoot);
	CheckSource(http, PackSource::Modrinth, "fabulously optimized", workRoot);
	CheckSource(http, PackSource::FTB, "", workRoot);
	CheckSource(http, PackSource::ATLauncher, "", workRoot);
	CheckSource(http, PackSource::Technic, "tekkit", workRoot);
	CheckClientOnlyLookup(http);
	CheckCategories(http);
	CheckPaging(http, PackSource::Modrinth, "");
	CheckPaging(http, PackSource::FTB, "");
	CheckPaging(http, PackSource::ATLauncher, "");
	CheckPaging(http, PackSource::Technic, "");
	if (!CurseForgeApiKey().empty())
		CheckPaging(http, PackSource::CurseForge, "");
	CheckPackCreation(http, workRoot);
	CheckLegacyForgeServer(http, workRoot, "1.7.10", "1.7.10-10.13.4.1614-1.7.10", "technic", "bin/modpack.jar");
	CheckLegacyForgeServer(http, workRoot, "1.6.4", "1.6.4-9.11.1.965", "atlauncher", "jarmods/minecraftforge.zip");
	if (!CurseForgeApiKey().empty()) {
		CheckSource(http, PackSource::CurseForge, "", workRoot);
		CheckCurseForgeContent(http, workRoot);
	} else
		std::cout << "SKIP  CurseForge: no API key\n";
	std::cout << (g_failures == 0 ? "All live checks passed\n" : std::to_string(g_failures) + " live check(s) failed\n");
	return g_failures == 0 ? 0 : 1;
}

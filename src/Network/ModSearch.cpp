#include "ModSearch.h"

#include "Network/HttpClient.h"
#include "Util/Paths.h"
#include "Util/SharedFiles.h"
#include "Util/Sha1.h"
#include "Util/Json.h"

#include <nlohmann/json.hpp>

#include <deque>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char* kApiBase = "https://api.modrinth.com/v2";

// Modrinth's project type, and the URL path its pages use.
static const char* ProjectType(ContentKind kind)
{
	switch (kind) {
	case ContentKind::ResourcePacks: return "resourcepack";
	case ContentKind::ShaderPacks: return "shader";
	default: return "mod";
	}
}

// The loaders a pack's mods may use. Quilt runs Fabric mods too.
static std::vector<std::string> ModLoaders(const ContentSearch& search)
{
	if (search.kind != ContentKind::Mods || search.loader.empty())
		return {};
	if (search.loader == "quilt")
		return { "quilt", "fabric" };
	return { search.loader };
}

bool SearchContent(IHttpClient& http, const ContentSearch& search, int limit, std::vector<ContentProject>& out,
				   std::string* err, const std::atomic<bool>* cancel)
{
	out.clear();
	// Facets are AND-ed groups of OR-ed values.
	json facets = json::array({ json::array({ std::string("project_type:") + ProjectType(search.kind) }) });
	if (!search.gameVersion.empty())
		facets.push_back(json::array({ "versions:" + search.gameVersion }));
	const std::vector<std::string> loaders = ModLoaders(search);
	if (!loaders.empty()) {
		json any = json::array();
		for (const std::string& loader : loaders)
			any.push_back("categories:" + loader);
		facets.push_back(any);
	}
	std::string url = std::string(kApiBase) + "/search?facets=" + IHttpClient::UrlEncode(facets.dump()) +
		"&limit=" + std::to_string(limit) + "&index=" + (search.query.empty() ? "downloads" : "relevance");
	if (!search.query.empty())
		url += "&query=" + IHttpClient::UrlEncode(search.query);
	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;
	try {
		const json j = json::parse(body);
		for (const auto& hit : j.at("hits")) {
			ContentProject project;
			project.id = StringOrEmpty(hit, "project_id");
			project.name = StringOrEmpty(hit, "title");
			project.summary = StringOrEmpty(hit, "description");
			project.author = StringOrEmpty(hit, "author");
			project.iconUrl = StringOrEmpty(hit, "icon_url");
			project.downloads = hit.value("downloads", 0ULL);
			const std::string slug = StringOrEmpty(hit, "slug");
			project.pageUrl = "https://modrinth.com/" + std::string(ProjectType(search.kind)) + "/" + (slug.empty() ? project.id : slug);
			if (!project.id.empty())
				out.push_back(std::move(project));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected search response: ") + ex.what();
		return false;
	}
	return true;
}

bool FindContentDownload(IHttpClient& http, const ContentSearch& search, const std::string& projectId,
						 ContentDownload& out, std::string* err, const std::atomic<bool>* cancel)
{
	out = ContentDownload{};
	std::string url = std::string(kApiBase) + "/project/" + IHttpClient::UrlEncode(projectId) + "/version";
	std::string separator = "?";
	if (!search.gameVersion.empty()) {
		url += separator + "game_versions=" + IHttpClient::UrlEncode(json::array({ search.gameVersion }).dump());
		separator = "&";
	}
	const std::vector<std::string> loaders = ModLoaders(search);
	if (!loaders.empty())
		url += separator + "loaders=" + IHttpClient::UrlEncode(json(loaders).dump());
	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;
	try {
		const json versions = json::parse(body); // newest first
		const json* chosen = nullptr;
		for (const auto& version : versions) {
			if (!chosen)
				chosen = &version;
			if (StringOrEmpty(version, "version_type") == "release") {
				chosen = &version;
				break;
			}
		}
		if (!chosen) {
			std::string what = search.gameVersion.empty() ? std::string() : "Minecraft " + search.gameVersion;
			if (!search.loader.empty() && search.kind == ContentKind::Mods)
				what += (what.empty() ? "" : " with ") + search.loader;
			if (err) *err = "No version for " + (what.empty() ? std::string("this pack") : what) + ".";
			return false;
		}
		const json files = chosen->value("files", json::array());
		const json* file = nullptr;
		for (const auto& candidate : files) {
			if (!file || candidate.value("primary", false))
				file = &candidate;
			if (candidate.value("primary", false))
				break;
		}
		if (!file) {
			if (err) *err = "That version has no files.";
			return false;
		}
		out.projectId = projectId;
		out.fileName = StringOrEmpty(*file, "filename");
		out.url = StringOrEmpty(*file, "url");
		out.size = file->value("size", 0ULL);
		if (file->contains("hashes") && (*file)["hashes"].is_object())
			out.sha1 = StringOrEmpty((*file)["hashes"], "sha1");
		for (const auto& dependency : chosen->value("dependencies", json::array())) {
			const std::string id = StringOrEmpty(dependency, "project_id");
			if (StringOrEmpty(dependency, "dependency_type") == "required" && !id.empty())
				out.requiredProjects.push_back(id);
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected versions response: ") + ex.what();
		return false;
	}
	if (out.url.empty() || out.fileName.empty()) {
		if (err) *err = "That version's file has no download link.";
		return false;
	}
	return true;
}

bool FindInstalledProjects(IHttpClient& http, const fs::path& folder, std::set<std::string>& projectIds,
						   std::string* err, const std::atomic<bool>* cancel)
{
	std::vector<std::string> sha1s;
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(folder, ec)) {
		// Turned-off copies count too: the project is still in the pack.
		if (!entry.is_regular_file(ec))
			continue;
		const std::string sha1 = Sha1File(entry.path().string());
		if (!sha1.empty())
			sha1s.push_back(sha1);
	}
	if (sha1s.empty())
		return true;
	std::string body;
	const json request = { { "hashes", sha1s }, { "algorithm", "sha1" } };
	if (!http.Post(std::string(kApiBase) + "/version_files", request.dump(), body, err, cancel))
		return false;
	try {
		const json versions = json::parse(body); // hash -> the version holding that file
		for (const auto& [hash, version] : versions.items()) {
			const std::string project = StringOrEmpty(version, "project_id");
			if (!project.empty())
				projectIds.insert(project);
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected version_files response: ") + ex.what();
		return false;
	}
	return true;
}

bool InstallContent(IHttpClient& http, const ContentSearch& search, const std::string& projectId,
					const fs::path& gameDir, std::vector<AddedContent>& added, std::vector<std::string>& notes,
					std::string* err, const std::atomic<bool>* cancel)
{
	added.clear();
	notes.clear();
	// Mods the pack has, so a dependency it already has (under any file name) isn't added twice.
	std::set<std::string> installed;
	FindInstalledProjects(http, gameDir / GetContentKind(ContentKind::Mods).folder, installed, nullptr, cancel);

	struct Wanted {
		std::string id;
		ContentKind kind;
		bool dependency;
	};
	std::deque<Wanted> queue{ { projectId, search.kind, false } };
	std::set<std::string> seen;
	constexpr std::size_t kMaxFiles = 32; // a dependency chain longer than this is almost certainly wrong
	while (!queue.empty() && seen.size() < kMaxFiles) {
		const Wanted wanted = queue.front();
		queue.pop_front();
		if (!seen.insert(wanted.id).second || (wanted.dependency && installed.count(wanted.id) > 0))
			continue;
		if (cancel && cancel->load()) {
			if (err) *err = "Cancelled";
			return false;
		}
		ContentSearch fit = search;
		fit.kind = wanted.kind;
		fit.query.clear();
		ContentDownload download;
		std::string error;
		if (!FindContentDownload(http, fit, wanted.id, download, &error, cancel)) {
			if (!wanted.dependency) {
				if (err) *err = error;
				return false;
			}
			notes.push_back("A required mod (" + wanted.id + ") couldn't be added: " + error);
			continue;
		}
		for (const std::string& dependency : download.requiredProjects)
			queue.push_back({ dependency, ContentKind::Mods, true });

		const fs::path folder = gameDir / GetContentKind(wanted.kind).folder;
		const std::string fileName = fs::u8path(download.fileName).filename().u8string();
		if (!IsPlainFileName(fileName)) {
			notes.push_back("Skipped a file with an unusable name: " + download.fileName);
			continue;
		}
		const fs::path target = folder / fs::u8path(fileName);
		fs::path off = target;
		off += ".disabled";
		std::error_code ec;
		if (fs::exists(target, ec) || fs::exists(off, ec))
			continue; // already in the pack
		fs::create_directories(folder, ec);
		// Another pack already has this exact file: link to the shared copy instead.
		const std::string shareKey = SharedFiles::KeyFor(download.sha1);
		if (!shareKey.empty() && SharedFiles::LinkTo(shareKey, 0, target)) {
			added.push_back({ wanted.kind, fileName, wanted.id });
			continue;
		}
		fs::path part = target;
		part += ".part";
		if (!http.DownloadToFile(download.url, part.string(), nullptr, &error, cancel)) {
			fs::remove(LongPath(part), ec);
			if (err) *err = "Couldn't download " + fileName + ": " + error;
			return false;
		}
		if (!download.sha1.empty() && Sha1File(LongPath(part).string()) != download.sha1) {
			fs::remove(LongPath(part), ec);
			if (err) *err = fileName + " didn't match its SHA-1 after downloading. Try again.";
			return false;
		}
		fs::rename(LongPath(part), LongPath(target), ec);
		if (ec) {
			const std::string why = ec.message();
			fs::remove(LongPath(part), ec);
			if (err) *err = "Couldn't save " + fileName + ": " + why;
			return false;
		}
		SharedFiles::Add(shareKey, target);
		added.push_back({ wanted.kind, fileName, wanted.id });
	}
	return true;
}

#include "Modrinth.h"
#include "HttpClient.h"
#include "Util/Json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>

using json = nlohmann::json;

static const char* kApiBase = "https://api.modrinth.com/v2";

static std::string JoinStrings(const json& array, const std::string& separator)
{
	std::string result;
	if (!array.is_array())
		return result;
	for (const auto& item : array) {
		if (!item.is_string())
			continue;
		if (!result.empty())
			result += separator;
		result += item.get<std::string>();
	}
	return result;
}

// Search hits use null for missing icons/gallery images, which json::value() would throw on.
bool SearchModrinthModpacks(HttpClient& http,
							const std::string& query,
							const ModrinthSearchOptions& options,
							int limit,
							ModrinthSearchResult& out,
							std::string* err,
							const std::atomic<bool>* cancel)
{
	out = {};
	// Facets are AND-ed groups: [["project_type:modpack"],["versions:1.20.1"],["categories:fabric"]].
	json facets = json::array({ json::array({ "project_type:modpack" }) });
	if (!options.gameVersion.empty())
		facets.push_back(json::array({ "versions:" + options.gameVersion }));
	if (!options.loader.empty())
		facets.push_back(json::array({ "categories:" + options.loader }));
	const std::string index = !options.index.empty() ? options.index : (query.empty() ? "downloads" : "relevance");
	std::string url = std::string(kApiBase) + "/search?facets=" +
		HttpClient::UrlEncode(facets.dump()) +
		"&limit=" + std::to_string(limit) +
		"&index=" + index;
	if (options.offset > 0)
		url += "&offset=" + std::to_string(options.offset);
	if (!query.empty())
		url += "&query=" + HttpClient::UrlEncode(query);

	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;

	try {
		const json j = json::parse(body);
		out.totalHits = j.value("total_hits", 0ULL);
		for (const auto& hit : j.at("hits")) {
			Modpack pack;
			pack.id = hit.value("slug", "");
			if (pack.id.empty())
				pack.id = hit.value("project_id", "");
			pack.name = hit.value("title", "");
			pack.description = hit.value("description", "");
			pack.author = hit.value("author", "");
			pack.downloads = hit.value("downloads", 0ULL);
			if (hit.contains("versions") && hit["versions"].is_array() && !hit["versions"].empty())
				pack.gameVersion = hit["versions"].back().get<std::string>();
			pack.iconUrl = StringOrEmpty(hit, "icon_url");
			pack.bannerUrl = StringOrEmpty(hit, "featured_gallery");
			if (pack.bannerUrl.empty() && hit.contains("gallery") && hit["gallery"].is_array() &&
				!hit["gallery"].empty() && hit["gallery"].front().is_string())
				pack.bannerUrl = hit["gallery"].front().get<std::string>();
			if (hit.contains("color") && hit["color"].is_number_unsigned())
				pack.color = hit["color"].get<std::uint32_t>() & 0xFFFFFFu;
			pack.source = PackSource::Modrinth;
			pack.sourceId = pack.id;
			pack.websiteUrl = "https://modrinth.com/modpack/" + pack.id;
			pack.filesLoaded = false;
			if (!pack.id.empty())
				out.packs.push_back(std::move(pack));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected search response: ") + ex.what();
		return false;
	}
	return true;
}

// Fills out from one entry of /project/{id}/version. False if it has no .mrpack file.
static bool ParseModrinthVersion(const json& version, ModrinthVersion& out)
{
	out = {};
	out.id = version.value("id", "");
	out.versionNumber = version.value("version_number", "");
	out.versionType = version.value("version_type", "");
	out.published = version.value("date_published", "").substr(0, 10);
	if (version.contains("game_versions") && version["game_versions"].is_array() && !version["game_versions"].empty())
		out.gameVersion = version["game_versions"].back().get<std::string>();
	out.loaders = JoinStrings(version.value("loaders", json::array()), ", ");

	// Use the primary file; fall back to the first .mrpack.
	const json files = version.value("files", json::array());
	const json* file = nullptr;
	for (const auto& candidate : files) {
		if (candidate.value("primary", false)) {
			file = &candidate;
			break;
		}
	}
	if (!file) {
		for (const auto& candidate : files) {
			const std::string name = candidate.value("filename", "");
			if (name.size() > 7 && name.compare(name.size() - 7, 7, ".mrpack") == 0) {
				file = &candidate;
				break;
			}
		}
	}
	if (!file)
		return false;
	PackFile packFile;
	packFile.name = file->value("filename", "");
	packFile.url = file->value("url", "");
	packFile.size = file->value("size", 0ULL);
	out.files.push_back(std::move(packFile));
	return true;
}

bool ListModrinthVersions(HttpClient& http,
						  const std::string& projectIdOrSlug,
						  std::vector<ModrinthVersion>& out,
						  std::string* err,
						  const std::atomic<bool>* cancel)
{
	out.clear();
	std::string body;
	if (!http.Get(std::string(kApiBase) + "/project/" + HttpClient::UrlEncode(projectIdOrSlug) + "/version", body, err, cancel))
		return false;
	try {
		for (const auto& version : json::parse(body)) {
			ModrinthVersion parsed;
			if (ParseModrinthVersion(version, parsed))
				out.push_back(std::move(parsed));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected version response: ") + ex.what();
		return false;
	}
	if (out.empty()) {
		if (err) *err = "This pack has no versions with an .mrpack file";
		return false;
	}
	return true;
}

bool FetchLatestModrinthVersion(HttpClient& http,
								const std::string& projectIdOrSlug,
								ModrinthVersion& out,
								std::string* err,
								const std::atomic<bool>* cancel)
{
	out = {};
	const std::string url = std::string(kApiBase) + "/project/" +
		HttpClient::UrlEncode(projectIdOrSlug) + "/version";

	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;

	try {
		const json versions = json::parse(body);
		if (!versions.is_array() || versions.empty()) {
			if (err) *err = "This pack has no published versions";
			return false;
		}

		// Versions come newest first; prefer the newest stable release.
		const json* chosen = &versions.front();
		for (const auto& version : versions) {
			if (version.value("version_type", "") == "release") {
				chosen = &version;
				break;
			}
		}
		if (!ParseModrinthVersion(*chosen, out)) {
			if (err) *err = "Latest version has no .mrpack file";
			return false;
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected version response: ") + ex.what();
		return false;
	}
	return true;
}

bool FetchModrinthPackMods(HttpClient& http,
						   const std::string& versionId,
						   std::vector<PackMod>& out,
						   std::string* err,
						   const std::atomic<bool>* cancel)
{
	out.clear();
	std::string body;
	if (!http.Get(std::string(kApiBase) + "/version/" + HttpClient::UrlEncode(versionId), body, err, cancel))
		return false;
	std::vector<std::string> ids;
	try {
		const json version = json::parse(body);
		for (const auto& dependency : version.value("dependencies", json::array())) {
			const std::string id = StringOrEmpty(dependency, "project_id");
			if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end())
				ids.push_back(id);
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected version response: ") + ex.what();
		return false;
	}
	// /projects takes a JSON array of ids; 100 at a time keeps the URL short.
	for (std::size_t start = 0; start < ids.size(); start += 100) {
		json batch = json::array();
		for (std::size_t i = start; i < ids.size() && i < start + 100; ++i)
			batch.push_back(ids[i]);
		if (!http.Get(std::string(kApiBase) + "/projects?ids=" + HttpClient::UrlEncode(batch.dump()), body, err, cancel))
			return false;
		try {
			for (const auto& project : json::parse(body)) {
				PackMod mod;
				mod.name = StringOrEmpty(project, "title");
				mod.summary = StringOrEmpty(project, "description");
				const std::string slug = StringOrEmpty(project, "slug");
				const std::string type = StringOrEmpty(project, "project_type");
				if (!slug.empty())
					mod.url = "https://modrinth.com/" + (type.empty() ? std::string("mod") : type) + "/" + slug;
				mod.clientOnly = StringOrEmpty(project, "server_side") == "unsupported";
				mod.serverOnly = StringOrEmpty(project, "client_side") == "unsupported";
				out.push_back(std::move(mod));
			}
		}
		catch (const std::exception& ex) {
			if (err) *err = std::string("Unexpected projects response: ") + ex.what();
			return false;
		}
	}
	return true;
}

bool FindModrinthClientOnly(HttpClient& http,
							const std::vector<std::string>& sha1s,
							std::set<std::string>& clientOnly,
							std::string* err,
							const std::atomic<bool>* cancel)
{
	if (sha1s.empty())
		return true;
	// 1. Hash -> the project that published the file.
	std::string body;
	const json request = { { "hashes", sha1s }, { "algorithm", "sha1" } };
	if (!http.Post(std::string(kApiBase) + "/version_files", request.dump(), body, err, cancel))
		return false;
	std::map<std::string, std::vector<std::string>> hashesByProject;
	try {
		const json versions = json::parse(body); // items() only refers to it, so it must outlive the loop
		for (const auto& [hash, version] : versions.items()) {
			const std::string project = StringOrEmpty(version, "project_id");
			if (!project.empty())
				hashesByProject[project].push_back(hash);
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected version_files response: ") + ex.what();
		return false;
	}
	// 2. Each project's server side, 100 projects per request.
	std::vector<std::string> projects;
	for (const auto& [project, hashes] : hashesByProject)
		projects.push_back(project);
	for (std::size_t start = 0; start < projects.size(); start += 100) {
		json batch = json::array();
		for (std::size_t i = start; i < projects.size() && i < start + 100; ++i)
			batch.push_back(projects[i]);
		if (!http.Get(std::string(kApiBase) + "/projects?ids=" + HttpClient::UrlEncode(batch.dump()), body, err, cancel))
			return false;
		try {
			for (const auto& project : json::parse(body)) {
				if (StringOrEmpty(project, "server_side") != "unsupported")
					continue;
				const auto it = hashesByProject.find(StringOrEmpty(project, "id"));
				if (it != hashesByProject.end())
					clientOnly.insert(it->second.begin(), it->second.end());
			}
		}
		catch (const std::exception& ex) {
			if (err) *err = std::string("Unexpected projects response: ") + ex.what();
			return false;
		}
	}
	return true;
}

bool FetchModrinthDescription(HttpClient& http,
							  const std::string& projectIdOrSlug,
							  std::string& out,
							  std::string* err,
							  const std::atomic<bool>* cancel)
{
	out.clear();
	std::string body;
	if (!http.Get(std::string(kApiBase) + "/project/" + HttpClient::UrlEncode(projectIdOrSlug), body, err, cancel))
		return false;
	try {
		out = StringOrEmpty(json::parse(body), "body");
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected project response: ") + ex.what();
		return false;
	}
	return true;
}

bool FetchModrinthBanner(HttpClient& http,
						 const std::string& projectIdOrSlug,
						 std::string& outUrl,
						 std::string* err,
						 const std::atomic<bool>* cancel)
{
	outUrl.clear();
	const std::string url = std::string(kApiBase) + "/project/" + HttpClient::UrlEncode(projectIdOrSlug);

	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;

	try {
		const json project = json::parse(body);
		const json gallery = project.value("gallery", json::array());
		// Prefer the featured image, then the gallery's first (lowest ordering) image.
		const json* chosen = nullptr;
		for (const auto& image : gallery) {
			if (image.value("featured", false)) {
				chosen = &image;
				break;
			}
		}
		if (!chosen && !gallery.empty())
			chosen = &gallery.front();
		if (chosen) {
			// raw_url is the original upload; url may be a 350px thumbnail.
			outUrl = StringOrEmpty(*chosen, "raw_url");
			if (outUrl.empty())
				outUrl = StringOrEmpty(*chosen, "url");
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected project response: ") + ex.what();
		return false;
	}
	return true;
}

bool FetchModrinthChangelog(HttpClient& http,
							const std::string& projectIdOrSlug,
							const std::string& versionId,
							const std::string& versionNumber,
							std::string& out,
							std::string* err,
							const std::atomic<bool>* cancel)
{
	out.clear();
	std::string body;
	const std::string url = versionId.empty()
		? std::string(kApiBase) + "/project/" + HttpClient::UrlEncode(projectIdOrSlug) + "/version"
		: std::string(kApiBase) + "/version/" + HttpClient::UrlEncode(versionId);
	if (!http.Get(url, body, err, cancel))
		return false;
	try {
		const json j = json::parse(body);
		if (!versionId.empty()) {
			out = StringOrEmpty(j, "changelog");
			return true;
		}
		for (const auto& version : j) {
			if (version.value("version_number", "") == versionNumber) {
				out = StringOrEmpty(version, "changelog");
				return true;
			}
		}
		if (err) *err = "Version " + versionNumber + " wasn't found on Modrinth";
		return false;
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected version response: ") + ex.what();
		return false;
	}
}

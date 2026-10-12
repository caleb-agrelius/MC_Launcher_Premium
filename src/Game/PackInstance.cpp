#include "PackInstance.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char* kPreparedMarker = ".mclp-prepared";

// Rejects absolute paths and paths that climb out of the instance, like the zip extractor does.
static bool IsSafeRelativePath(const fs::path& path)
{
	if (path.empty() || path.has_root_name() || path.has_root_directory())
		return false;
	const fs::path normalized = path.lexically_normal();
	return !normalized.empty() && *normalized.begin() != "..";
}

bool ReadMrpackIndex(const fs::path& extractedDir, MrpackIndex& out, std::string* err, bool forServer)
{
	out = MrpackIndex{};
	const fs::path path = extractedDir / "modrinth.index.json";
	std::ifstream ifs(path);
	if (!ifs.is_open()) {
		if (err) *err = "This pack has no modrinth.index.json. Install its files again.";
		return false;
	}
	try {
		json j;
		ifs >> j;
		if (j.value("game", "minecraft") != "minecraft") {
			if (err) *err = "This pack isn't for Minecraft";
			return false;
		}
		out.name = j.value("name", "");
		out.versionId = j.value("versionId", "");
		const json deps = j.value("dependencies", json::object());
		out.minecraftVersion = deps.value("minecraft", "");
		const std::pair<const char*, LoaderKind> loaders[] = {
			{ "fabric-loader", LoaderKind::Fabric },
			{ "quilt-loader", LoaderKind::Quilt },
			{ "neoforge", LoaderKind::NeoForge },
			{ "forge", LoaderKind::Forge },
		};
		for (const auto& [key, kind] : loaders) {
			if (deps.contains(key)) {
				out.loader = kind;
				out.loaderVersion = deps.value(key, "");
				break;
			}
		}
		if (out.minecraftVersion.empty()) {
			if (err) *err = "The pack doesn't say which Minecraft version it needs";
			return false;
		}

		for (const auto& file : j.value("files", json::array())) {
			const json env = file.value("env", json::object());
			if (env.value(forServer ? "server" : "client", "required") == "unsupported")
				continue;
			const fs::path relative = fs::u8path(file.value("path", ""));
			if (!IsSafeRelativePath(relative)) {
				if (err) *err = "Unsafe file path in pack: " + file.value("path", "");
				return false;
			}
			const json urls = file.value("downloads", json::array());
			if (!urls.is_array() || urls.empty() || !urls[0].is_string())
				continue;
			DownloadJob job;
			job.url = urls[0].get<std::string>();
			job.path = relative.lexically_normal();
			job.sha1 = file.value("hashes", json::object()).value("sha1", "");
			job.size = file.value("fileSize", 0ULL);
			out.files.push_back(std::move(job));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = "Couldn't read modrinth.index.json: " + std::string(ex.what());
		return false;
	}
	return true;
}

// Copies a folder's contents into target, replacing files that exist.
static bool CopyTree(const fs::path& source, const fs::path& target, std::string* err)
{
	std::error_code ec;
	if (!fs::is_directory(source, ec))
		return true;
	fs::copy(source, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
	if (ec) {
		if (err) *err = "Couldn't copy " + source.filename().string() + ": " + ec.message();
		return false;
	}
	return true;
}

bool PrepareInstance(HttpClient& http, const fs::path& extractedDir, const fs::path& instanceDir,
	const MrpackIndex& index, const ProgressFn& progress, std::string* err, const std::atomic<bool>* cancel)
{
	std::error_code ec;
	fs::create_directories(instanceDir, ec);

	std::vector<DownloadJob> jobs = index.files;
	for (DownloadJob& job : jobs)
		job.path = instanceDir / job.path;
	if (!DownloadAll(http, jobs, "Downloading mods", progress, err, cancel))
		return false;

	const fs::path marker = instanceDir / kPreparedMarker;
	std::string prepared;
	{
		std::ifstream in(marker);
		std::getline(in, prepared);
	}
	const std::string current = index.versionId.empty() ? "unknown" : index.versionId;
	if (prepared == current)
		return true;

	if (progress)
		progress("Copying pack configuration", -1.0);
	if (!CopyTree(extractedDir / "overrides", instanceDir, err) ||
		!CopyTree(extractedDir / "client-overrides", instanceDir, err))
		return false;
	std::ofstream(marker, std::ios::trunc) << current << "\n";
	return true;
}

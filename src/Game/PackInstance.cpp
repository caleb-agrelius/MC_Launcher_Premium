#include "PackInstance.h"

#include <miniz/miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>

#include "Util/Sha1.h"

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

bool IsWholePackInstall(const fs::path& installDir)
{
	std::error_code ec;
	return fs::is_regular_file(installDir / "pack.json", ec);
}

bool ParseLoaderName(const std::string& name, LoaderKind& out)
{
	std::string lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (lower.empty() || lower == "vanilla" || lower == "none")
		out = LoaderKind::Vanilla;
	else if (lower == "forge")
		out = LoaderKind::Forge;
	else if (lower == "neoforge")
		out = LoaderKind::NeoForge;
	else if (lower == "fabric" || lower == "fabric-loader")
		out = LoaderKind::Fabric;
	else if (lower == "quilt" || lower == "quilt-loader")
		out = LoaderKind::Quilt;
	else
		return false;
	return true;
}

std::string NormalizeLoaderVersion(const std::string& minecraftVersion, const std::string& version)
{
	std::string v = version;
	for (const char* prefix : { "neoforge-", "forge-", "fabric-loader-", "fabric-", "quilt-loader-", "quilt-" }) {
		const std::size_t length = std::strlen(prefix);
		if (v.size() > length && std::equal(prefix, prefix + length, v.begin(),
				[](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); })) {
			v.erase(0, length);
			break;
		}
	}
	if (!minecraftVersion.empty()) {
		const std::string lead = minecraftVersion + "-";
		const std::string trail = "-" + minecraftVersion;
		if (v.size() > lead.size() && v.compare(0, lead.size(), lead) == 0)
			v.erase(0, lead.size());
		if (v.size() > trail.size() && v.compare(v.size() - trail.size(), trail.size(), trail) == 0)
			v.erase(v.size() - trail.size());
	}
	return v;
}

// Finds the loader in a version JSON shipped with a pack (Technic's bin/version.json, or
// the one inside bin/modpack.jar) by looking for the loader's own library.
static bool LoaderFromVersionJson(const std::string& text, InstalledPack& out)
{
	json j;
	try {
		j = json::parse(text);
	}
	catch (...) {
		return false;
	}
	if (out.minecraftVersion.empty())
		out.minecraftVersion = j.value("inheritsFrom", j.value("jar", std::string()));
	const std::pair<const char*, LoaderKind> libraries[] = {
		{ "net.neoforged:neoforge:", LoaderKind::NeoForge },
		{ "net.neoforged:forge:", LoaderKind::NeoForge },
		{ "net.minecraftforge:forge:", LoaderKind::Forge },
		{ "net.minecraftforge:minecraftforge:", LoaderKind::Forge },
		{ "net.fabricmc:fabric-loader:", LoaderKind::Fabric },
		{ "org.quiltmc:quilt-loader:", LoaderKind::Quilt },
	};
	for (const auto& library : j.value("libraries", json::array())) {
		const std::string name = library.value("name", "");
		for (const auto& [prefix, kind] : libraries) {
			const std::string p = prefix;
			if (name.compare(0, p.size(), p) != 0)
				continue;
			std::string version = name.substr(p.size());
			version = version.substr(0, version.find(':')); // drop a classifier
			out.loader = kind;
			out.loaderVersion = NormalizeLoaderVersion(out.minecraftVersion, version);
			return !out.loaderVersion.empty();
		}
	}
	return false;
}

static bool ReadZipEntry(const fs::path& zipPath, const char* entry, std::string& out)
{
	mz_zip_archive zip;
	std::memset(&zip, 0, sizeof(zip));
	if (!mz_zip_reader_init_file(&zip, zipPath.string().c_str(), 0))
		return false;
	std::size_t size = 0;
	void* data = mz_zip_reader_extract_file_to_heap(&zip, entry, &size, 0);
	mz_zip_reader_end(&zip);
	if (!data)
		return false;
	out.assign(static_cast<const char*>(data), size);
	mz_free(data);
	return true;
}

bool ReadInstalledPack(const fs::path& installDir, InstalledPack& out, std::string* err)
{
	out = InstalledPack{};
	std::ifstream ifs(installDir / "pack.json");
	if (!ifs.is_open()) {
		if (err) *err = "This pack has no pack.json. Install it again.";
		return false;
	}
	std::string loaderName;
	std::string loaderVersion;
	try {
		json j;
		ifs >> j;
		out.minecraftVersion = j.value("minecraft", "");
		loaderName = j.value("loader", "");
		loaderVersion = j.value("loaderVersion", "");
		out.version = j.value("version", "");
		if (out.version.empty())
			out.version = j.value("sourceVersionId", "");
		for (const auto& path : j.value("clientOnlyFiles", json::array())) {
			if (path.is_string())
				out.clientOnlyFiles.push_back(path.get<std::string>());
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Couldn't read pack.json: ") + ex.what();
		return false;
	}
	if (!ParseLoaderName(loaderName, out.loader)) {
		if (err) *err = "This pack uses a mod loader the launcher doesn't support: " + loaderName;
		return false;
	}
	out.loaderVersion = NormalizeLoaderVersion(out.minecraftVersion, loaderVersion);
	out.loaderSource = "pack.json";

	// No loader declared: look at what the pack ships in bin/ (Technic).
	if (out.loader == LoaderKind::Vanilla) {
		const fs::path bin = installDir / "minecraft" / "bin";
		std::string text;
		std::ifstream versionFile(bin / "version.json");
		if (versionFile.is_open()) {
			text.assign(std::istreambuf_iterator<char>(versionFile), std::istreambuf_iterator<char>());
			if (LoaderFromVersionJson(text, out))
				out.loaderSource = "bin/version.json";
		}
		std::error_code ec;
		if (out.loader == LoaderKind::Vanilla && fs::is_regular_file(bin / "modpack.jar", ec) &&
			ReadZipEntry(bin / "modpack.jar", "version.json", text) && LoaderFromVersionJson(text, out))
			out.loaderSource = "bin/modpack.jar";
	}

	if (out.minecraftVersion.empty()) {
		if (err) *err = "The pack doesn't say which Minecraft version it needs";
		return false;
	}
	if (out.loader != LoaderKind::Vanilla && out.loaderVersion.empty()) {
		if (err) *err = std::string("The pack doesn't say which ") + LoaderName(out.loader) + " version it needs";
		return false;
	}
	return true;
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

	// The marker holds the prepared pack version, then the files that version listed.
	const fs::path marker = instanceDir / kPreparedMarker;
	std::string prepared;
	std::vector<std::string> previousFiles;
	{
		std::ifstream in(marker);
		std::getline(in, prepared);
		for (std::string line; std::getline(in, line);) {
			if (!line.empty())
				previousFiles.push_back(line);
		}
	}
	const std::string current = index.versionId.empty() ? "unknown" : index.versionId;
	if (prepared == current)
		return true;

	// A new pack version: delete files the old version listed that this one doesn't
	// (mods the pack dropped). Files the player added themselves were never listed.
	std::set<std::string> currentFiles;
	for (const DownloadJob& job : index.files)
		currentFiles.insert(job.path.generic_string());
	for (const std::string& old : previousFiles) {
		const fs::path relative = fs::u8path(old);
		if (currentFiles.count(old) == 0 && IsSafeRelativePath(relative))
			fs::remove(instanceDir / relative, ec);
	}

	if (progress)
		progress("Copying pack configuration", -1.0);
	if (!CopyTree(extractedDir / "overrides", instanceDir, err) ||
		!CopyTree(extractedDir / "client-overrides", instanceDir, err))
		return false;
	std::ofstream out(marker, std::ios::trunc);
	out << current << "\n";
	for (const std::string& file : currentFiles)
		out << file << "\n";
	return true;
}

bool RepairInstance(HttpClient& http, const fs::path& extractedDir, const fs::path& instanceDir,
	const MrpackIndex& index, const ProgressFn& progress, std::string* err, const std::atomic<bool>* cancel)
{
	std::size_t checked = 0;
	for (const DownloadJob& job : index.files) {
		if (cancel && cancel->load()) {
			if (err) *err = "Cancelled";
			return false;
		}
		if (progress)
			progress("Checking files (" + std::to_string(checked) + "/" + std::to_string(index.files.size()) + ")",
				static_cast<double>(checked) / static_cast<double>(std::max<std::size_t>(1, index.files.size())));
		++checked;
		const fs::path path = instanceDir / job.path;
		std::error_code ec;
		if (job.sha1.empty() || !fs::is_regular_file(path, ec))
			continue;
		if (Sha1File(path.string()) != job.sha1)
			fs::remove(path, ec);
	}
	// Downloads what's missing or was just removed.
	return PrepareInstance(http, extractedDir, instanceDir, index, progress, err, cancel);
}

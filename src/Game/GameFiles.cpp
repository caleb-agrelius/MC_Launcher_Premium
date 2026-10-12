#include "GameFiles.h"

#include "Network/HttpClient.h"
#include "Storage/Installer.h"
#include "Util/Sha1.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char* kVersionManifestUrl = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
static const char* kJavaRuntimesUrl =
	"https://launchermeta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";
static const char* kAssetBaseUrl = "https://resources.download.minecraft.net/";
static const char* kDefaultLibraryRepo = "https://libraries.minecraft.net/";
static constexpr int kDownloadThreads = 8;
static constexpr int kDownloadAttempts = 3;

#ifdef _WIN32
static const char* kOsName = "windows";
static const char* kJavaPlatform = "windows-x64";
#elif defined(__APPLE__)
static const char* kOsName = "osx";
static const char* kJavaPlatform = "mac-os";
#else
static const char* kOsName = "linux";
static const char* kJavaPlatform = "linux";
#endif

static bool ReadJsonFile(const fs::path& path, json& out, std::string* err)
{
	std::ifstream ifs(path);
	if (!ifs.is_open()) {
		if (err) *err = "Missing " + path.string();
		return false;
	}
	try {
		ifs >> out;
	}
	catch (const std::exception& ex) {
		if (err) *err = "Couldn't read " + path.string() + ": " + ex.what();
		return false;
	}
	return true;
}

static bool GetJson(HttpClient& http, const std::string& url, json& out, std::string* err, const std::atomic<bool>* cancel)
{
	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;
	try {
		out = json::parse(body);
	}
	catch (const std::exception& ex) {
		if (err) *err = "Bad JSON from " + url + ": " + ex.what();
		return false;
	}
	return true;
}

static std::string StringOr(const json& object, const char* key, const std::string& fallback = {})
{
	const auto it = object.find(key);
	return (it != object.end() && it->is_string()) ? it->get<std::string>() : fallback;
}

// ---- Downloads ----

static bool NeedsDownload(const DownloadJob& job)
{
	std::error_code ec;
	if (!fs::is_regular_file(job.path, ec))
		return true;
	return job.size != 0 && fs::file_size(job.path, ec) != job.size;
}

// A temporary name no other download uses, so two packs (or two launcher
// windows) fetching the same shared file don't write to the same .part file.
static fs::path UniquePartPath(const fs::path& path)
{
	static std::atomic<unsigned> counter{ 0 };
	static const unsigned processTag = static_cast<unsigned>(
		std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFF);
	fs::path part = path;
	part += "." + std::to_string(processTag) + "-" + std::to_string(counter.fetch_add(1)) + ".part";
	return part;
}

static bool DownloadOne(HttpClient& http, const DownloadJob& job, std::string& error, const std::atomic<bool>* cancel)
{
	std::error_code ec;
	fs::create_directories(job.path.parent_path(), ec);
	const fs::path part = UniquePartPath(job.path);
	for (int attempt = 1; attempt <= kDownloadAttempts; ++attempt) {
		if (!NeedsDownload(job))
			return true; // someone else finished it meanwhile
		if (!http.DownloadToFile(job.url, part.string(), nullptr, &error, cancel)) {
			if (cancel && cancel->load())
				return false;
			continue;
		}
		if (!job.sha1.empty() && Sha1File(part.string()) != job.sha1) {
			error = "Checksum mismatch for " + job.url;
			fs::remove(part, ec);
			continue;
		}
		// Replacing an existing file can fail briefly while Windows still has it open (an
		// antivirus scan, or a Java process that just exited), so retry, then remove it first.
		std::error_code renameError;
		for (int tries = 0; tries < 10; ++tries) {
			fs::rename(part, job.path, renameError);
			if (!renameError)
				return true;
			if (tries == 5) {
				std::error_code removeError;
				fs::remove(job.path, removeError);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
		fs::remove(part, ec);
		if (!NeedsDownload(job))
			return true; // another downloader put it in place first
		error = "Couldn't save " + job.path.string() + ": " + renameError.message();
		return false;
	}
	if (!NeedsDownload(job))
		return true;
	error ="Download failed after " + std::to_string(kDownloadAttempts) + " tries: " + job.url + " (" + error + ")";
	return false;
}

bool DownloadAll(HttpClient& http, const std::vector<DownloadJob>& jobs, const std::string& step,
	const ProgressFn& progress, std::string* err, const std::atomic<bool>* cancel)
{
	std::vector<const DownloadJob*> pending;
	for (const DownloadJob& job : jobs) {
		if (!NeedsDownload(job))
			continue;
		if (job.url.empty()) {
			if (err) *err = "Missing file with no download URL: " + job.path.string();
			return false;
		}
		pending.push_back(&job);
	}
	if (pending.empty())
		return true;

	std::atomic<std::size_t> next{ 0 };
	std::atomic<std::size_t> done{ 0 };
	std::atomic<bool> failed{ false };
	std::mutex errorMutex;
	std::string firstError;
	auto worker = [&]() {
		for (;;) {
			if (failed.load() || (cancel && cancel->load()))
				return;
			const std::size_t index = next.fetch_add(1);
			if (index >= pending.size())
				return;
			std::string error;
			if (!DownloadOne(http, *pending[index], error, cancel)) {
				std::lock_guard<std::mutex> lock(errorMutex);
				if (!failed.exchange(true))
					firstError = error;
				return;
			}
			done.fetch_add(1);
		}
	};
	std::vector<std::thread> threads;
	const std::size_t threadCount = std::min<std::size_t>(kDownloadThreads, pending.size());
	for (std::size_t i = 0; i < threadCount; ++i)
		threads.emplace_back(worker);

	// Report progress from this thread only, so callers don't need to be thread-safe.
	for (;;) {
		const std::size_t finished = done.load();
		if (progress)
			progress(step + " (" + std::to_string(finished) + "/" + std::to_string(pending.size()) + ")",
				static_cast<double>(finished) / static_cast<double>(pending.size()));
		if (finished == pending.size() || failed.load() || (cancel && cancel->load()))
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	for (std::thread& thread : threads)
		thread.join();

	if (cancel && cancel->load()) {
		if (err) *err = "Cancelled";
		return false;
	}
	if (failed.load()) {
		if (err) *err = firstError;
		return false;
	}
	return true;
}

// ---- Version JSON ----

std::string MavenPath(const std::string& coordinate)
{
	std::string coords = coordinate;
	std::string extension = "jar";
	const std::size_t at = coords.find('@');
	if (at != std::string::npos) {
		extension = coords.substr(at + 1);
		coords.resize(at);
	}
	std::vector<std::string> parts;
	std::size_t start = 0;
	for (;;) {
		const std::size_t colon = coords.find(':', start);
		parts.push_back(coords.substr(start, colon - start));
		if (colon == std::string::npos)
			break;
		start = colon + 1;
	}
	if (parts.size() < 3)
		return {};
	std::string group = parts[0];
	std::replace(group.begin(), group.end(), '.', '/');
	const std::string& artifact = parts[1];
	const std::string& version = parts[2];
	std::string file = artifact + "-" + version;
	if (parts.size() > 3 && !parts[3].empty())
		file += "-" + parts[3];
	return group + "/" + artifact + "/" + version + "/" + file + "." + extension;
}

bool RulesAllow(const json& rules)
{
	if (!rules.is_array() || rules.empty())
		return true;
	bool allowed = false;
	for (const auto& rule : rules) {
		bool matches = true;
		if (rule.contains("os")) {
			const json& os = rule["os"];
			if (os.contains("name") && os["name"] != kOsName)
				matches = false;
			if (os.contains("arch") && os["arch"] != "x64" && os["arch"] != "x86_64")
				matches = false; // "x86" rules are for 32-bit systems
		}
		if (rule.contains("features") && rule["features"].is_object()) {
			for (const auto& [feature, value] : rule["features"].items()) {
				(void)feature;
				if (value != false)
					matches = false;
			}
		}
		if (matches)
			allowed = StringOr(rule, "action") == "allow";
	}
	return allowed;
}

static std::string LibraryKey(const std::string& name, const std::string& classifier)
{
	std::string coords = name.substr(0, name.find('@'));
	// group:artifact[:version[:classifier]] -> group:artifact[:classifier]
	std::vector<std::string> parts;
	std::size_t start = 0;
	for (;;) {
		const std::size_t colon = coords.find(':', start);
		parts.push_back(coords.substr(start, colon - start));
		if (colon == std::string::npos)
			break;
		start = colon + 1;
	}
	std::string key = parts.size() >= 2 ? parts[0] + ":" + parts[1] : coords;
	const std::string cls = !classifier.empty() ? classifier : (parts.size() > 3 ? parts[3] : std::string());
	if (!cls.empty())
		key += ":" + cls;
	return key;
}

static std::string RepoUrl(const json& library)
{
	std::string repo = StringOr(library, "url", kDefaultLibraryRepo);
	if (!repo.empty() && repo.back() != '/')
		repo += '/';
	return repo;
}

static void ResolveLibraries(const fs::path& root, const json& libraries, ResolvedVersion& out,
	std::set<std::string>& seenClasspath, std::set<std::string>& seenNatives)
{
	if (!libraries.is_array())
		return;
	const fs::path libraryDir = root / "libraries";
	for (const auto& library : libraries) {
		if (!RulesAllow(library.value("rules", json())))
			continue;
		const std::string name = StringOr(library, "name");
		if (name.empty())
			continue;
		const json downloads = library.value("downloads", json::object());

		// Old-style natives: a classifier jar that is extracted, not put on the classpath.
		const bool hasNatives = library.contains("natives") && library["natives"].is_object();
		if (hasNatives) {
			std::string classifier = StringOr(library["natives"], kOsName);
			if (!classifier.empty()) {
				const std::size_t arch = classifier.find("${arch}");
				if (arch != std::string::npos)
					classifier.replace(arch, 7, "64");
				const std::string key = LibraryKey(name, classifier);
				if (seenNatives.insert(key).second) {
					LibraryFile file;
					file.key = key;
					const json classifiers = downloads.value("classifiers", json::object());
					if (classifiers.contains(classifier)) {
						const json& artifact = classifiers[classifier];
						const std::string path = StringOr(artifact, "path", MavenPath(name + ":" + classifier));
						file.path = libraryDir / fs::u8path(path);
						file.url = StringOr(artifact, "url");
						file.sha1 = StringOr(artifact, "sha1");
						file.size = artifact.value("size", 0ULL);
					} else {
						const std::string path = MavenPath(name + ":" + classifier);
						file.path = libraryDir / fs::u8path(path);
						file.url = RepoUrl(library) + path;
					}
					out.natives.push_back(std::move(file));
				}
			}
		}

		LibraryFile file;
		file.key = LibraryKey(name, "");
		if (downloads.contains("artifact")) {
			const json& artifact = downloads["artifact"];
			const std::string path = StringOr(artifact, "path", MavenPath(name));
			file.path = libraryDir / fs::u8path(path);
			file.url = StringOr(artifact, "url");
			file.sha1 = StringOr(artifact, "sha1");
			file.size = artifact.value("size", 0ULL);
		} else if (!hasNatives) {
			const std::string path = MavenPath(name);
			file.path = libraryDir / fs::u8path(path);
			file.url = RepoUrl(library) + path;
			file.sha1 = StringOr(library, "sha1");
			file.size = library.value("size", 0ULL);
		} else {
			continue; // natives-only library
		}
		if (seenClasspath.insert(file.key).second)
			out.classpath.push_back(std::move(file));
	}
}

bool InstallVanillaJson(HttpClient& http, const fs::path& root, const std::string& minecraftVersion,
	std::string* err, const std::atomic<bool>* cancel)
{
	const fs::path path = root / "versions" / minecraftVersion / (minecraftVersion + ".json");
	std::error_code ec;
	if (fs::is_regular_file(path, ec))
		return true;
	json manifest;
	if (!GetJson(http, kVersionManifestUrl, manifest, err, cancel))
		return false;
	for (const auto& version : manifest.value("versions", json::array())) {
		if (StringOr(version, "id") != minecraftVersion)
			continue;
		DownloadJob job;
		job.url = StringOr(version, "url");
		job.sha1 = StringOr(version, "sha1");
		job.path = path;
		return DownloadAll(http, { job }, "Downloading Minecraft " + minecraftVersion, nullptr, err, cancel);
	}
	if (err) *err = "Minecraft " + minecraftVersion + " isn't in Mojang's version list";
	return false;
}

bool ResolveVersion(const fs::path& root, const std::string& versionId, ResolvedVersion& out, std::string* err)
{
	// Load the chain: [versionId, parent, grandparent, ...]
	std::vector<json> chain;
	std::string id = versionId;
	while (!id.empty()) {
		if (chain.size() > 8) {
			if (err) *err = "Version " + versionId + " inherits too deeply";
			return false;
		}
		json j;
		if (!ReadJsonFile(root / "versions" / id / (id + ".json"), j, err))
			return false;
		id = StringOr(j, "inheritsFrom");
		chain.push_back(std::move(j));
	}

	out = ResolvedVersion{};
	out.id = versionId;
	// Merge from the base (vanilla) up, so children override and append.
	for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
		const json& j = *it;
		const std::string thisId = StringOr(j, "id");
		if (j.contains("mainClass"))
			out.mainClass = StringOr(j, "mainClass");
		if (j.contains("type"))
			out.type = StringOr(j, "type");
		if (j.contains("arguments")) {
			for (const auto& arg : j["arguments"].value("jvm", json::array()))
				out.jvmArgs.push_back(arg);
			for (const auto& arg : j["arguments"].value("game", json::array()))
				out.gameArgs.push_back(arg);
		}
		if (j.contains("minecraftArguments"))
			out.legacyGameArgs = StringOr(j, "minecraftArguments");
		if (j.contains("assetIndex"))
			out.assetIndex = j["assetIndex"];
		if (j.contains("downloads") && j["downloads"].contains("client")) {
			out.clientDownload = j["downloads"]["client"];
			out.jarId = thisId;
		}
		if (j.contains("jar"))
			out.jarId = StringOr(j, "jar");
		if (j.contains("logging") && j["logging"].contains("client"))
			out.logging = j["logging"]["client"];
		if (j.contains("javaVersion")) {
			out.javaComponent = StringOr(j["javaVersion"], "component", out.javaComponent);
			out.javaMajor = j["javaVersion"].value("majorVersion", out.javaMajor);
		}
	}
	if (out.jarId.empty())
		out.jarId = StringOr(chain.back(), "id");
	if (out.mainClass.empty()) {
		if (err) *err = "Version " + versionId + " has no main class";
		return false;
	}

	// Libraries: the most specific version first, so a loader's copy of a library wins.
	std::set<std::string> seenClasspath;
	std::set<std::string> seenNatives;
	for (const json& j : chain)
		ResolveLibraries(root, j.value("libraries", json::array()), out, seenClasspath, seenNatives);
	return true;
}

fs::path ClientJarPath(const fs::path& root, const ResolvedVersion& version)
{
	return root / "versions" / version.jarId / (version.jarId + ".jar");
}

fs::path NativesDir(const fs::path& root, const ResolvedVersion& version)
{
	return root / "versions" / version.id / "natives";
}

static DownloadJob JobFor(const LibraryFile& file)
{
	DownloadJob job;
	job.url = file.url;
	job.path = file.path;
	job.sha1 = file.sha1;
	job.size = file.size;
	return job;
}

bool InstallGameFiles(HttpClient& http, const fs::path& root, const ResolvedVersion& version,
	const ProgressFn& progress, std::string* err, const std::atomic<bool>* cancel)
{
	std::vector<DownloadJob> jobs;
	if (!version.clientDownload.is_null()) {
		DownloadJob client;
		client.url = StringOr(version.clientDownload, "url");
		client.sha1 = StringOr(version.clientDownload, "sha1");
		client.size = version.clientDownload.value("size", 0ULL);
		client.path = ClientJarPath(root, version);
		jobs.push_back(client);
	}
	for (const LibraryFile& file : version.classpath)
		jobs.push_back(JobFor(file));
	for (const LibraryFile& file : version.natives)
		jobs.push_back(JobFor(file));
	if (!version.logging.is_null() && version.logging.contains("file")) {
		const json& file = version.logging["file"];
		DownloadJob job;
		job.url = StringOr(file, "url");
		job.sha1 = StringOr(file, "sha1");
		job.size = file.value("size", 0ULL);
		job.path = root / "assets" / "log_configs" / StringOr(file, "id");
		jobs.push_back(job);
	}
	if (!DownloadAll(http, jobs, "Downloading Minecraft libraries", progress, err, cancel))
		return false;

	// Natives: extract every old-style native jar into one folder, minus metadata.
	// A marker skips this on later launches, when a running game may have the DLLs locked.
	const fs::path nativesMarker = NativesDir(root, version) / ".extracted";
	std::error_code markerError;
	if (!version.natives.empty() && !fs::exists(nativesMarker, markerError)) {
		const fs::path nativesDir = NativesDir(root, version);
		std::error_code ec;
		fs::create_directories(nativesDir, ec);
		for (const LibraryFile& file : version.natives) {
			if (progress)
				progress("Extracting native libraries", -1.0);
			std::string error;
			if (!InstallZip(file.path.string(), nativesDir.string(), nullptr, &error, cancel)) {
				if (err) *err = "Couldn't extract " + file.path.filename().string() + ": " + error;
				return false;
			}
		}
		fs::remove_all(nativesDir / "META-INF", ec);
		std::ofstream(nativesMarker) << "ok\n";
	}

	// Assets: the index lists every object by hash.
	if (version.assetIndex.is_null())
		return true;
	const std::string indexId = StringOr(version.assetIndex, "id");
	const fs::path assetsDir = root / "assets";
	DownloadJob indexJob;
	indexJob.url = StringOr(version.assetIndex, "url");
	indexJob.sha1 = StringOr(version.assetIndex, "sha1");
	indexJob.size = version.assetIndex.value("size", 0ULL);
	indexJob.path = assetsDir / "indexes" / (indexId + ".json");
	if (!DownloadAll(http, { indexJob }, "Downloading asset index", progress, err, cancel))
		return false;
	json index;
	if (!ReadJsonFile(indexJob.path, index, err))
		return false;

	std::vector<DownloadJob> assetJobs;
	std::set<std::string> seenHashes;
	const json objects = index.value("objects", json::object());
	for (const auto& [name, object] : objects.items()) {
		(void)name;
		const std::string hash = StringOr(object, "hash");
		if (hash.size() < 2 || !seenHashes.insert(hash).second)
			continue;
		DownloadJob job;
		job.url = kAssetBaseUrl + hash.substr(0, 2) + "/" + hash;
		job.sha1 = hash;
		job.size = object.value("size", 0ULL);
		job.path = assetsDir / "objects" / hash.substr(0, 2) / hash;
		assetJobs.push_back(std::move(job));
	}
	if (!DownloadAll(http, assetJobs, "Downloading assets", progress, err, cancel))
		return false;

	// Very old versions (before 1.7.3) read assets by name from a "virtual" folder.
	if (index.value("virtual", false) || index.value("map_to_resources", false)) {
		const fs::path virtualDir = assetsDir / "virtual" / indexId;
		for (const auto& [name, object] : objects.items()) {
			const std::string hash = StringOr(object, "hash");
			const fs::path target = virtualDir / fs::u8path(name);
			std::error_code ec;
			if (fs::exists(target, ec))
				continue;
			fs::create_directories(target.parent_path(), ec);
			fs::copy_file(assetsDir / "objects" / hash.substr(0, 2) / hash, target, ec);
		}
	}
	return true;
}

bool InstallJavaRuntime(HttpClient& http, const fs::path& root, const std::string& component, bool windowed,
	fs::path& javaExe, const ProgressFn& progress, std::string* err, const std::atomic<bool>* cancel)
{
	const fs::path runtimeDir = root / "runtime" / component;
#ifdef _WIN32
	javaExe = runtimeDir / "bin" / (windowed ? "javaw.exe" : "java.exe");
#else
	(void)windowed;
	javaExe = runtimeDir / "bin" / "java";
#endif
	const fs::path marker = runtimeDir / ".manifest-sha1";

	if (progress)
		progress("Checking Java " + component, -1.0);
	json runtimes;
	if (!GetJson(http, kJavaRuntimesUrl, runtimes, err, cancel)) {
		// Offline: use an already-installed runtime.
		std::error_code ec;
		if (fs::is_regular_file(javaExe, ec) && fs::is_regular_file(marker, ec))
			return true;
		return false;
	}
	const json entries = runtimes.value(kJavaPlatform, json::object()).value(component, json::array());
	if (!entries.is_array() || entries.empty()) {
		if (err) *err = std::string("Mojang has no ") + component + " Java runtime for " + kJavaPlatform;
		return false;
	}
	const json manifestInfo = entries[0].value("manifest", json::object());
	const std::string manifestSha1 = StringOr(manifestInfo, "sha1");

	{
		std::ifstream in(marker);
		std::string installed;
		std::getline(in, installed);
		std::error_code ec;
		if (!manifestSha1.empty() && installed == manifestSha1 && fs::is_regular_file(javaExe, ec))
			return true;
	}

	json manifest;
	if (!GetJson(http, StringOr(manifestInfo, "url"), manifest, err, cancel))
		return false;
	std::vector<DownloadJob> jobs;
	const json files = manifest.value("files", json::object()); // items() must not outlive this
	for (const auto& [path, entry] : files.items()) {
		const fs::path target = runtimeDir / fs::u8path(path);
		const std::string type = StringOr(entry, "type");
		std::error_code ec;
		if (type == "directory") {
			fs::create_directories(target, ec);
		} else if (type == "file") {
			const json raw = entry.value("downloads", json::object()).value("raw", json::object());
			DownloadJob job;
			job.url = StringOr(raw, "url");
			job.sha1 = StringOr(raw, "sha1");
			job.size = raw.value("size", 0ULL);
			job.path = target;
			job.executable = entry.value("executable", false);
			jobs.push_back(std::move(job));
		}
		// "link" entries only exist in the macOS/Linux runtimes.
	}
	if (!DownloadAll(http, jobs, "Downloading Java " + component, progress, err, cancel))
		return false;
#ifndef _WIN32
	for (const DownloadJob& job : jobs) {
		if (job.executable) {
			std::error_code ec;
			fs::permissions(job.path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
		}
	}
#endif
	std::ofstream(marker, std::ios::trunc) << manifestSha1 << "\n";
	return true;
}

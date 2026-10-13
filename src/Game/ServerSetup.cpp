#include "ServerSetup.h"

#include "Game/JavaFinder.h"
#include "Game/Loaders.h"
#include "Game/PackInstance.h"
#include "Network/HttpClient.h"
#include "Storage/Installer.h"
#include "Util/Paths.h"
#include "Util/Text.h"
#include "Util/Process.h"
#include "Util/Sha1.h"
#include "Util/TracedFs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <regex>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char* kInfoFile = ".mclp-server.json";
static const char* kPreparedMarker = ".mclp-server-prepared";

bool CanCreateServer(const fs::path& extractedDir)
{
	std::error_code ec;
	return fs::is_regular_file(extractedDir / "modrinth.index.json", ec) ||
		(IsWholePackInstall(extractedDir) && fs::is_directory(extractedDir / "minecraft", ec));
}

static std::string LowercaseText(std::string text)
{
	for (char& c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return text;
}

// Top-level entries of a client game folder that a server doesn't use: worlds, client
// settings and caches, visual packs, and the launcher's own copies of the game.
static bool IsClientOnlyEntry(const std::string& topLevel)
{
	static const char* const kNames[] = {
		"saves", "screenshots", "logs", "crash-reports", "resourcepacks", "shaderpacks", "texturepacks",
		"bin", "versions", "libraries", "assets", "natives", "jarmods", "downloads", ".fabric", ".mixin.out",
		"options.txt", "optionsof.txt", "optionsshaders.txt", "servers.dat", "servers.dat_old", "usercache.json",
		"usernamecache.json", "realms_persistence.json", "launcher_profiles.json", "command_history.txt", "hotbar.nbt",
	};
	const std::string name = LowercaseText(topLevel);
	for (const char* candidate : kNames) {
		if (name == candidate)
			return true;
	}
	return false;
}

// Copies a whole-pack install's game folder into the server folder. Mod jars are copied
// when the server doesn't have them yet; client-only ones arrive as "<name>.disabled".
// Everything else is copied with copyConfigs (the first time each pack version is set
// up), without overwriting the server's own changes. Files with a .disabled copy on the
// server are never copied again.
static bool CopyWholePackFiles(const ServerSetupRequest& request, const InstalledPack& installed, bool copyConfigs,
	std::vector<std::string>& notes, const ProgressFn& progress, std::string* err, const std::atomic<bool>* cancel)
{
	const fs::path gameDir = ExtendedPath(request.extractedDir / "minecraft");
	std::error_code ec;
	if (!fs::is_directory(gameDir, ec)) {
		if (err) *err = "The pack's game folder is missing. Repair or reinstall the pack.";
		return false;
	}
	std::vector<fs::path> files;
	for (auto it = fs::recursive_directory_iterator(gameDir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		const fs::path relative = it->path().lexically_relative(gameDir);
		if (it.depth() == 0 && IsClientOnlyEntry(relative.string())) {
			if (it->is_directory(ec))
				it.disable_recursion_pending();
			continue;
		}
		if (it->is_regular_file(ec))
			files.push_back(relative);
	}
	if (ec) {
		if (err) *err = "Couldn't read the pack's game folder: " + ec.message();
		return false;
	}
	auto isModJar = [](const fs::path& relative) {
		return !relative.empty() && LowercaseText(relative.begin()->string()) == "mods" &&
			LowercaseText(relative.extension().string()) == ".jar";
	};
	auto disabledPath = [](fs::path path) {
		path += ".disabled";
		return path;
	};

	// Client-only mods: what the source said at install time, then Modrinth by hash for
	// the mods the server doesn't have yet.
	std::set<std::string> clientOnly; // lowercase generic relative paths
	for (const std::string& path : installed.clientOnlyFiles)
		clientOnly.insert(LowercaseText(fs::path(path).lexically_normal().generic_string()));
	if (request.findClientOnly) {
		if (progress)
			progress("Checking which mods are client-only", -1.0);
		std::map<std::string, std::string> pathByHash;
		for (const fs::path& relative : files) {
			const fs::path destination = request.serverDir / relative;
			if (!isModJar(relative) || clientOnly.count(LowercaseText(relative.generic_string())) ||
				fs::exists(destination, ec) || fs::exists(disabledPath(destination), ec))
				continue;
			// Mod jars sit at short paths, which the narrow-path hasher can open.
			const std::string hash = LowercaseText(Sha1File((request.extractedDir / "minecraft" / relative).string()));
			if (!hash.empty())
				pathByHash[hash] = LowercaseText(relative.generic_string());
		}
		std::vector<std::string> hashes;
		for (const auto& [hash, path] : pathByHash)
			hashes.push_back(hash);
		std::set<std::string> found;
		if (!request.findClientOnly(hashes, found))
			notes.push_back("Modrinth couldn't be reached to check for client-only mods. If the server crashes, "
				"turn off client-only mods under Mods.");
		for (const std::string& hash : found) {
			const auto it = pathByHash.find(LowercaseText(hash));
			if (it != pathByHash.end())
				clientOnly.insert(it->second);
		}
	}

	if (progress)
		progress("Copying pack files", -1.0);
	std::vector<std::string> turnedOff;
	for (std::size_t i = 0; i < files.size(); ++i) {
		if (cancel && cancel->load()) {
			if (err) *err = "Cancelled";
			return false;
		}
		const fs::path& relative = files[i];
		const fs::path destination = ExtendedPath(request.serverDir / relative);
		if (fs::exists(disabledPath(destination), ec))
			continue;
		fs::path target = destination;
		if (isModJar(relative)) {
			if (fs::exists(destination, ec))
				continue;
			if (clientOnly.count(LowercaseText(relative.generic_string()))) {
				target = disabledPath(destination);
				turnedOff.push_back(relative.filename().string());
			}
		} else if (!copyConfigs) {
			continue;
		}
		TracedFs::CreateDirectories(target.parent_path(), ec);
		if (!ec)
			TracedFs::CopyFile(gameDir / relative, target, fs::copy_options::skip_existing, ec);
		if (ec) {
			if (err) *err = "Couldn't copy " + relative.generic_string() + ": " + ec.message();
			return false;
		}
		if (progress && (i % 64 == 0))
			progress("Copying pack files", static_cast<double>(i) / static_cast<double>(files.size()));
	}
	if (!turnedOff.empty()) {
		std::string names;
		for (std::size_t i = 0; i < turnedOff.size() && i < 5; ++i)
			names += (i ? ", " : "") + turnedOff[i];
		if (turnedOff.size() > 5)
			names += " and " + std::to_string(turnedOff.size() - 5) + " more";
		notes.push_back(std::to_string(turnedOff.size()) + " client-only mod(s) were turned off: " + names +
			". Turn them on under Mods if the server needs them.");
	}
	return true;
}

static bool GetJson(IHttpClient& http, const std::string& url, json& out, std::string* err, const std::atomic<bool>* cancel)
{
	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;
	try {
		out = json::parse(body);
	}
	catch (const std::exception& ex) {
		if (err) *err = "Unexpected response from " + url + ": " + ex.what();
		return false;
	}
	return true;
}

static bool CopyOverrides(const fs::path& extractedDir, const fs::path& target, std::string* err)
{
	std::error_code ec;
	// Server overrides win over common overrides, while existing server configuration
	// and worlds survive updates. Client overrides are never copied.
	// Long-path forms throughout: pack configs nest deeply (e.g. config/modpack_defaults/...).
	std::map<fs::path, fs::path> files;
	for (const char* name : { "overrides", "server-overrides" }) {
		const fs::path source = ExtendedPath(extractedDir / name);
		if (!fs::is_directory(source, ec))
			continue;
		for (auto it = fs::recursive_directory_iterator(source, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
			if (it->is_regular_file(ec))
				files[it->path().lexically_relative(source)] = it->path();
		}
		if (ec) {
			if (err) *err = std::string("Couldn't read the pack's ") + name + " folder: " + ec.message();
			return false;
		}
	}
	for (const auto& [relative, source] : files) {
		const fs::path destination = ExtendedPath(target / relative);
		fs::path disabled = destination;
		disabled += ".disabled";
		if (fs::exists(disabled, ec))
			continue; // turned off by the player (Servers > Mods)
		TracedFs::CreateDirectories(destination.parent_path(), ec);
		if (!ec)
			TracedFs::CopyFile(source, destination, fs::copy_options::skip_existing, ec);
		if (ec) {
			if (err) *err = "Couldn't copy " + relative.string() + ": " + ec.message();
			return false;
		}
	}
	return true;
}

// The vanilla server jar, from the version JSON (downloads.server).
static bool DownloadVanillaServer(IHttpClient& http, const fs::path& gameRoot, const std::string& minecraftVersion,
	const fs::path& target, const ProgressFn& progress, std::string* err, const std::atomic<bool>* cancel)
{
	if (!InstallVanillaJson(http, gameRoot, minecraftVersion, err, cancel))
		return false;
	json version;
	try {
		std::ifstream in(gameRoot / "versions" / minecraftVersion / (minecraftVersion + ".json"));
		in >> version;
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Couldn't read the Minecraft version file: ") + ex.what();
		return false;
	}
	const json server = version.value("downloads", json::object()).value("server", json::object());
	if (!server.contains("url")) {
		if (err) *err = "Mojang doesn't offer a server for Minecraft " + minecraftVersion;
		return false;
	}
	DownloadJob job;
	job.url = server.value("url", "");
	job.sha1 = server.value("sha1", "");
	job.size = server.value("size", 0ULL);
	job.path = target;
	return DownloadAll(http, { job }, "Downloading the Minecraft " + minecraftVersion + " server", progress, err, cancel);
}

bool ReadForgeServerClassPath(const fs::path& forgeJar, std::vector<std::string>& out, std::string* err)
{
	out.clear();
	std::string manifest;
	if (!ReadZipEntry(forgeJar.u8string(), "META-INF/MANIFEST.MF", manifest)) {
		if (err) *err = forgeJar.filename().u8string() + " has no manifest";
		return false;
	}
	// Manifest lines are wrapped at 72 bytes; a line starting with a space continues the last one.
	std::vector<std::string> lines;
	std::size_t start = 0;
	while (start < manifest.size()) {
		std::size_t end = manifest.find('\n', start);
		if (end == std::string::npos)
			end = manifest.size();
		std::string line = manifest.substr(start, end - start);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (!line.empty() && line[0] == ' ' && !lines.empty())
			lines.back() += line.substr(1);
		else
			lines.push_back(line);
		start = end + 1;
	}
	for (const std::string& line : lines) {
		if (line.rfind("Class-Path:", 0) != 0)
			continue;
		std::istringstream entries(line.substr(11));
		for (std::string entry; entries >> entry;) {
			// Only plain relative paths: they're joined to the server folder.
			if (IsSafeRelativePath(entry))
				out.push_back(entry);
		}
	}
	if (out.empty()) {
		if (err) *err = forgeJar.filename().u8string() + " doesn't list the server's libraries (no Class-Path in its manifest)";
		return false;
	}
	return true;
}

const std::vector<std::string>& LegacyForgeLibraryMirrors()
{
	static const std::vector<std::string> mirrors = {
		"https://libraries.minecraft.net/",
		"https://maven.minecraftforge.net/",
		"https://repo1.maven.org/maven2/",
	};
	return mirrors;
}

// An old Forge server (Minecraft 1.6 to 1.12) from the Forge universal jar the pack ships (Technic's
// bin/modpack.jar, or an ATLauncher jar mod). Forge's installers for these versions download from
// retired URLs, so this lays the server out the way they did: the jar in the server folder, and the
// files its manifest's Class-Path names beside it (the vanilla server and libraries/). Libraries
// the client already installed are copied from gameRoot/libraries/. Sets jarName to the jar to run.
static bool InstallLegacyForgeServer(IHttpClient& http, const fs::path& gameRoot, const fs::path& forgeJar,
	const std::string& minecraftVersion, const std::string& forgeVersion, const fs::path& serverDir, std::string& jarName,
	const ProgressFn& progress, std::string* err, const std::atomic<bool>* cancel)
{
	std::vector<std::string> classPath;
	if (!ReadForgeServerClassPath(forgeJar, classPath, err))
		return false;
	std::error_code ec;
	jarName = "forge-" + minecraftVersion + "-" + forgeVersion + "-universal.jar";
	TracedFs::CopyFile(forgeJar, serverDir / jarName, fs::copy_options::overwrite_existing, ec);
	if (ec) {
		if (err) *err = "Couldn't copy the pack's Forge jar to the server: " + ec.message();
		return false;
	}
	std::size_t done = 0;
	for (const std::string& entry : classPath) {
		if (cancel && cancel->load()) {
			if (err) *err = "Cancelled";
			return false;
		}
		++done;
		const fs::path target = serverDir / fs::u8path(entry);
		if (entry.rfind("libraries/", 0) != 0) {
			// The vanilla server, named as Forge expects (minecraft_server.<version>.jar).
			if (entry.rfind("minecraft_server.", 0) == 0 &&
				!DownloadVanillaServer(http, gameRoot, minecraftVersion, target, progress, err, cancel))
				return false;
			continue;
		}
		if (fs::file_size(target, ec) > 0 && !ec)
			continue;
		if (progress)
			progress("Downloading Forge libraries (" + std::to_string(done) + "/" + std::to_string(classPath.size()) + ")",
				static_cast<double>(done) / static_cast<double>(classPath.size()));
		TracedFs::CreateDirectories(target.parent_path(), ec);
		const fs::path installed = gameRoot / fs::u8path(entry);
		ec.clear();
		if (fs::file_size(installed, ec) > 0 && !ec) {
			TracedFs::CopyFile(installed, target, fs::copy_options::overwrite_existing, ec);
			if (!ec)
				continue;
		}
		const std::string mavenPath = entry.substr(std::strlen("libraries/"));
		std::string lastError;
		bool downloaded = false;
		for (const std::string& mirror : LegacyForgeLibraryMirrors()) {
			DownloadJob job;
			job.url = mirror + mavenPath;
			job.path = target;
			if (DownloadAll(http, { job }, "Downloading Forge libraries", nullptr, &lastError, cancel)) {
				downloaded = true;
				break;
			}
			if (cancel && cancel->load())
				break;
		}
		if (!downloaded) {
			if (err) *err = "Couldn't download the Forge library " + fs::u8path(mavenPath).filename().u8string() + ": " + lastError;
			return false;
		}
	}
	return true;
}

const char* ForgeArgsFileName()
{
#ifdef _WIN32
	return "win_args.txt";
#else
	return "unix_args.txt";
#endif
}

// Finds Forge/NeoForge's argument file for this OS (newer versions), relative to the server folder.
static std::string FindForgeArgsFile(const fs::path& serverDir, const MrpackIndex& index)
{
	std::error_code ec;
	const bool modernNeoForge = index.loader == LoaderKind::NeoForge && index.minecraftVersion != "1.20.1";
	const fs::path base = index.loader == LoaderKind::Forge ? "libraries/net/minecraftforge/forge"
		: modernNeoForge ? "libraries/net/neoforged/neoforge" : "libraries/net/neoforged/forge";
	const std::string version = modernNeoForge ? index.loaderVersion : index.minecraftVersion + "-" + index.loaderVersion;
	const fs::path relative = base / version / ForgeArgsFileName();
	return fs::is_regular_file(serverDir / relative, ec) ? relative.generic_string() : std::string();
}

// Older Forge servers start from a jar named forge-<mc>-<version>[-universal].jar.
static std::string FindForgeServerJar(const fs::path& serverDir, const MrpackIndex& index)
{
	std::error_code ec;
	const std::string base = "forge-" + index.minecraftVersion + "-" + index.loaderVersion;
	for (const std::string& name : { base + ".jar", base + "-universal.jar" }) {
		if (fs::is_regular_file(serverDir / name, ec))
			return name;
	}
	return {};
}

static bool RunInstaller(const std::vector<std::vector<std::string>>& attempts, const fs::path& workDir,
	const fs::path& log, std::string* err, const std::atomic<bool>* cancel)
{
	for (const auto& command : attempts) {
		int exitCode = -1;
		std::string error;
		if (!RunProcess(command, workDir, log, true, &exitCode, &error, cancel, true)) {
			if (err) *err = error;
			return false;
		}
		if (exitCode == 0)
			return true;
	}
	if (err) *err = "The server installer failed. See " + log.string();
	return false;
}

bool SetupServer(IHttpClient& http, const ServerSetupRequest& request, ServerInfo& out, const ProgressFn& progress,
	std::string* err, const std::atomic<bool>* cancel)
{
	out = ServerInfo{};
	if (cancel && cancel->load()) {
		if (err) *err = "Cancelled";
		return false;
	}
	const fs::path& dir = request.serverDir;
	std::error_code ec;
	TracedFs::CreateDirectories(dir, ec);
	if (ec) {
		if (err) *err = "Couldn't create the server folder: " + ec.message();
		return false;
	}

	// 1. The pack's server-side files and its overrides.
	if (progress)
		progress("Reading modpack", -1.0);
	MrpackIndex index;
	InstalledPack installed;
	const bool wholePack = IsWholePackInstall(request.extractedDir);
	if (wholePack) {
		// The installed game folder holds everything; copied below with the configuration.
		if (!ReadInstalledPack(request.extractedDir, installed, err))
			return false;
		index.minecraftVersion = installed.minecraftVersion;
		index.loader = installed.loader;
		index.loaderVersion = installed.loaderVersion;
		index.versionId = installed.version;
	} else if (!ReadMrpackIndex(request.extractedDir, index, err, true)) {
		return false;
	}
	std::vector<DownloadJob> jobs;
	for (DownloadJob job : index.files) {
		if (request.removedFiles.count(job.path.generic_u8string()) > 0)
			continue; // the player removed it from the pack
		job.path = dir / job.path;
		// A file renamed to "<name>.disabled" was turned off by the player (e.g. a mod that
		// breaks the server); don't download it again.
		fs::path disabled = job.path;
		disabled += ".disabled";
		if (!fs::exists(disabled, ec))
			jobs.push_back(std::move(job));
	}
	if (!DownloadAll(http, jobs, "Downloading server mods", progress, err, cancel))
		return false;
	const std::string current = index.versionId.empty() ? "unknown" : index.versionId;
	// The marker holds the prepared pack version, then the files that version listed.
	std::string prepared;
	std::vector<std::string> previousFiles;
	{
		std::ifstream in(dir / kPreparedMarker);
		std::getline(in, prepared);
		for (std::string line; std::getline(in, line);) {
			if (!line.empty())
				previousFiles.push_back(line);
		}
	}
	if (wholePack && !CopyWholePackFiles(request, installed, prepared != current, out.notes, progress, err, cancel))
		return false;
	// Mods the player added to the pack. Whole-pack servers usually have them already (copied
	// with the game folder above).
	std::vector<std::string> addedNames;
	for (const fs::path& mod : request.addedMods) {
		const fs::path target = dir / "mods" / mod.filename();
		fs::path disabled = target;
		disabled += ".disabled";
		if (!fs::is_regular_file(mod, ec) || fs::exists(target, ec) || fs::exists(disabled, ec))
			continue;
		TracedFs::CreateDirectories(target.parent_path(), ec);
		TracedFs::CopyFile(mod, target, ec);
		if (ec) {
			if (err) *err = "Couldn't copy " + mod.filename().u8string() + " to the server: " + ec.message();
			return false;
		}
		addedNames.push_back(mod.filename().u8string());
	}
	if (!addedNames.empty()) {
		std::string note = "Copied " + std::to_string(addedNames.size()) + " mod(s) you added to the pack: ";
		for (std::size_t i = 0; i < addedNames.size() && i < 5; ++i)
			note += (i ? ", " : "") + addedNames[i];
		if (addedNames.size() > 5)
			note += ", ...";
		note += ". Turn off any that only work on the client.";
		out.notes.push_back(note);
	}
	std::set<std::string> currentFiles;
	for (const DownloadJob& job : index.files)
		currentFiles.insert(job.path.generic_string());
	if (prepared != current) {
		// Another pack version: remove mods the old one listed and this one doesn't.
		for (const std::string& old : previousFiles) {
			const fs::path relative = fs::u8path(old).lexically_normal();
			if (currentFiles.count(old) == 0 && !relative.empty() && relative.is_relative() && *relative.begin() != "..") {
				TracedFs::Remove(dir / relative, ec);
				fs::path disabled = dir / relative; // also a copy the player turned off (Servers > Mods)
				disabled += ".disabled";
				TracedFs::Remove(disabled, ec);
			}
		}
		if (progress)
			progress("Copying pack configuration", -1.0);
		if (!wholePack && !CopyOverrides(request.extractedDir, dir, err))
			return false;
		auto marker = TracedFs::OpenWrite(dir / kPreparedMarker, std::ios::trunc);
		marker << current << "\n";
		for (const std::string& file : currentFiles)
			marker << file << "\n";
		marker.close();
		if (!marker.good()) {
			if (err) *err = "Couldn't save the server configuration marker";
			return false;
		}
	}

	// 2. Java: the player's, or the Mojang runtime this Minecraft version uses.
	if (progress)
		progress("Preparing Minecraft " + index.minecraftVersion, -1.0);
	if (!InstallVanillaJson(http, request.gameRoot, index.minecraftVersion, err, cancel))
		return false;
	ResolvedVersion vanilla;
	if (!ResolveVersion(request.gameRoot, index.minecraftVersion, vanilla, err))
		return false;
	fs::path java;
	if (!request.javaPath.empty()) {
		java = fs::path(request.javaPath);
		if (fs::is_directory(java, ec))
			java = fs::is_directory(java / "bin", ec) ? java / "bin" / JavaExecutableName(false) : java / JavaExecutableName(false);
		if (java.filename() == "javaw.exe" && fs::is_regular_file(java.parent_path() / "java.exe", ec))
			java = java.parent_path() / "java.exe";
		if (!fs::is_regular_file(java, ec)) {
			if (err) *err = "The Java chosen in Settings > Minecraft wasn't found: " + request.javaPath;
			return false;
		}
	} else if (const std::vector<JavaInstall> installs = FindJavaInstalls(request.gameRoot);
			const JavaInstall* installed = PickJava(installs, vanilla.javaMajor)) {
		java = installed->java; // automatic: an installed Java with the major version Minecraft needs
	} else if (!InstallJavaRuntime(http, request.gameRoot, vanilla.javaComponent, false, java, progress, err, cancel)) {
		return false;
	}

	// 3. The loader's server.
	const fs::path log = request.gameRoot / "logs" / "server-installer.log";
	out.minecraftVersion = index.minecraftVersion;
	out.loader = LoaderName(index.loader);
	out.loaderVersion = index.loaderVersion;
	out.packVersion = index.versionId;
	out.java = fs::absolute(java).string();
	out.javaMajor = vanilla.javaMajor;
	switch (index.loader) {
	case LoaderKind::Vanilla:
		if (!DownloadVanillaServer(http, request.gameRoot, index.minecraftVersion, dir / "server.jar", progress, err, cancel))
			return false;
		out.launchArgs = { "-jar", "server.jar", "nogui" };
		break;
	case LoaderKind::Fabric: {
		// Fabric's server launcher jar, plus the vanilla server it starts (server.jar).
		if (!DownloadVanillaServer(http, request.gameRoot, index.minecraftVersion, dir / "server.jar", progress, err, cancel))
			return false;
		json installers;
		if (!GetJson(http, "https://meta.fabricmc.net/v2/versions/installer", installers, err, cancel))
			return false;
		std::string installerVersion;
		for (const auto& item : installers) {
			if (item.value("stable", false)) {
				installerVersion = item.value("version", "");
				break;
			}
		}
		if (installerVersion.empty() && installers.is_array() && !installers.empty())
			installerVersion = installers[0].value("version", "");
		if (installerVersion.empty()) {
			if (err) *err = "Fabric didn't return a server installer version";
			return false;
		}
		DownloadJob job;
		job.url = "https://meta.fabricmc.net/v2/versions/loader/" + index.minecraftVersion + "/" + index.loaderVersion + "/" +
			installerVersion + "/server/jar";
		job.path = dir / ("fabric-server-" + index.minecraftVersion + "-" + index.loaderVersion + "-" + installerVersion + ".jar");
		if (!DownloadAll(http, { job }, "Downloading the Fabric server", progress, err, cancel))
			return false;
		out.launchArgs = { "-jar", job.path.filename().string(), "nogui" };
		break;
	}
	case LoaderKind::Quilt: {
		std::string metadata;
		if (!http.Get("https://maven.quiltmc.org/repository/release/org/quiltmc/quilt-installer/maven-metadata.xml",
				metadata, err, cancel))
			return false;
		std::smatch match;
		if (!std::regex_search(metadata, match, std::regex("<release>([^<]+)</release>"))) {
			if (err) *err = "Couldn't find the latest Quilt installer";
			return false;
		}
		const std::string installerVersion = match[1];
		DownloadJob job;
		job.url = "https://maven.quiltmc.org/repository/release/org/quiltmc/quilt-installer/" + installerVersion +
			"/quilt-installer-" + installerVersion + ".jar";
		job.path = request.gameRoot / "installers" / ("quilt-installer-" + installerVersion + ".jar");
		if (!DownloadAll(http, { job }, "Downloading the Quilt installer", progress, err, cancel))
			return false;
		if (progress)
			progress("Installing the Quilt server", -1.0);
		if (!RunInstaller({ { out.java, "-jar", job.path.string(), "install", "server", index.minecraftVersion,
				index.loaderVersion, "--download-server", "--install-dir=" + fs::absolute(dir).string() } },
				dir, log, err, cancel))
			return false;
		out.launchArgs = { "-jar", "quilt-server-launch.jar", "nogui" };
		break;
	}
	case LoaderKind::Forge:
	case LoaderKind::NeoForge: {
		if (index.loader == LoaderKind::Forge && wholePack && !installed.loaderJar.empty() &&
			IsLegacyForgeMinecraft(index.minecraftVersion)) {
			// Old Forge found in the pack's own jar: built from that jar, not Forge's installer.
			std::string jar;
			if (!InstallLegacyForgeServer(http, request.gameRoot, installed.loaderJar, index.minecraftVersion,
					index.loaderVersion, dir, jar, progress, err, cancel))
				return false;
			if (NeedsLegacyJavaFixer(index.minecraftVersion, index.loaderVersion) &&
				!DownloadAll(http, { LegacyJavaFixerJob(dir) }, "Downloading LegacyJavaFixer", progress, err, cancel))
				return false;
			// Like Play: current Java 8 rejects the SHA-1 signatures old FML checks.
			out.launchArgs = { "-Dfml.ignoreInvalidMinecraftCertificates=true", "-Dfml.ignorePatchDiscrepancies=true",
				"-jar", jar, "nogui" };
			out.notes.push_back("Forge " + index.loaderVersion + " was set up from the pack's own " + installed.loaderSource + ".");
			break;
		}
		fs::path installer;
		if (!DownloadForgeInstaller(http, request.gameRoot, index.loader, index.minecraftVersion, index.loaderVersion,
				installer, progress, err, cancel))
			return false;
		if (progress)
			progress(std::string("Running the ") + out.loader + " server installer (this can take a few minutes)", -1.0);
		const std::string target = fs::absolute(dir).string();
		if (!RunInstaller({ { out.java, "-jar", installer.string(), "--installServer", target },
				{ out.java, "-jar", installer.string(), "--install-server", target } }, dir, log, err, cancel))
			return false;
		const std::string argsFile = FindForgeArgsFile(dir, index);
		if (!argsFile.empty()) {
			out.launchArgs = { "@" + argsFile, "nogui" };
		} else {
			const std::string jar = FindForgeServerJar(dir, index);
			if (jar.empty()) {
				if (err) *err = std::string("The ") + out.loader + " installer finished, but no server jar was found. See " + log.string();
				return false;
			}
			out.launchArgs = { "-jar", jar, "nogui" };
		}
		break;
	}
	}

	if (cancel && cancel->load()) {
		if (err) *err = "Cancelled";
		return false;
	}
	if (!fs::exists(dir / "server.properties", ec) && !WriteServerProperties(dir, ServerProperties{}, err))
		return false;
	json info = {
		{ "minecraftVersion", out.minecraftVersion },
		{ "loader", out.loader },
		{ "loaderVersion", out.loaderVersion },
		{ "packVersion", out.packVersion },
		{ "java", out.java },
		{ "launchArgs", out.launchArgs },
		{ "notes", out.notes },
		{ "javaMajor", out.javaMajor },
	};
	const fs::path temporary = dir / (std::string(kInfoFile) + ".tmp");
	auto saved = TracedFs::OpenWrite(temporary, std::ios::trunc);
	saved << info.dump(2) << "\n";
	saved.close();
	if (saved.good())
		TracedFs::Rename(temporary, dir / kInfoFile, ec);
	if (!saved.good() || ec) {
		if (err) *err = "Couldn't save the server launch information";
		return false;
	}
	return true;
}

bool LoadServerInfo(const fs::path& serverDir, ServerInfo& out)
{
	out = ServerInfo{};
	std::ifstream in(serverDir / kInfoFile);
	if (!in.is_open())
		return false;
	try {
		json j;
		in >> j;
		out.minecraftVersion = j.value("minecraftVersion", "");
		out.loader = j.value("loader", "");
		out.loaderVersion = j.value("loaderVersion", "");
		out.packVersion = j.value("packVersion", "");
		out.java = j.value("java", "");
		out.launchArgs = j.value("launchArgs", std::vector<std::string>());
		out.notes = j.value("notes", std::vector<std::string>());
		out.javaMajor = j.value("javaMajor", 0);
	}
	catch (...) {
		return false;
	}
	return !out.java.empty() && !out.launchArgs.empty();
}

bool RemoveServer(const fs::path& serverDir, std::string* err)
{
	std::error_code ec;
	const fs::path absolute = fs::absolute(serverDir, ec).lexically_normal();
	if (ec || absolute.empty() || absolute == absolute.root_path()) {
		if (err) *err = "Not a server folder: " + serverDir.string();
		return false;
	}
	// Files deep inside can pass 260 characters even when serverDir doesn't.
	TracedFs::RemoveAll(ExtendedPath(absolute), ec);
	if (ec) {
		if (err) *err = "Couldn't delete " + serverDir.string() + ": " + ec.message() +
			". Close anything using its files and try again.";
		return false;
	}
	return true;
}

std::vector<ServerMod> ListServerMods(const fs::path& serverDir)
{
	return ListContent(serverDir, ContentKind::Mods);
}

bool SetServerModEnabled(const fs::path& serverDir, const std::string& fileName, bool enabled, std::string* err)
{
	return SetContentEnabled(serverDir / "mods", fileName, enabled, err);
}

bool HasServerFiles(const fs::path& serverDir)
{
	std::error_code ec;
	return fs::is_regular_file(serverDir / kInfoFile, ec);
}

std::vector<std::string> ServerCommand(const ServerInfo& info, int memoryMb)
{
	std::vector<std::string> command = { info.java, "-Xmx" + std::to_string(memoryMb) + "M" };
	command.insert(command.end(), info.launchArgs.begin(), info.launchArgs.end());
	return command;
}

// server.properties: key=value lines. Reading keeps it simple; writing keeps every line
// it doesn't change, like the game options writer.
static std::map<std::string, std::string> ReadPropertiesFile(const fs::path& path)
{
	std::map<std::string, std::string> values;
	std::ifstream in(path);
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		const std::size_t eq = line.find('=');
		if (eq != std::string::npos)
			values[line.substr(0, eq)] = line.substr(eq + 1);
	}
	return values;
}

static bool IsLocalhost(const std::string& address)
{
	return ToLower(address) == "localhost";
}

bool IsValidServerIp(const std::string& address)
{
	if (address.empty() || IsLocalhost(address))
		return true;
	return std::regex_match(address, std::regex(
		R"(((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d))"));
}

bool IsLoopbackServerIp(const std::string& address)
{
	return IsLocalhost(address) || (IsValidServerIp(address) && address.rfind("127.", 0) == 0);
}

ServerProperties ReadServerProperties(const fs::path& serverDir)
{
	ServerProperties props;
	const auto values = ReadPropertiesFile(serverDir / "server.properties");
	auto text = [&](const char* key, std::string& field) {
		const auto it = values.find(key);
		if (it != values.end())
			field = it->second;
	};
	auto number = [&](const char* key, int& field) {
		const auto it = values.find(key);
		if (it != values.end()) {
			try { field = std::stoi(it->second); } catch (...) {}
		}
	};
	auto flag = [&](const char* key, bool& field) {
		const auto it = values.find(key);
		if (it != values.end())
			field = it->second == "true";
	};
	number("server-port", props.port);
	text("server-ip", props.serverIp);
	number("max-players", props.maxPlayers);
	text("motd", props.motd);
	flag("online-mode", props.onlineMode);
	flag("pvp", props.pvp);
	text("difficulty", props.difficulty);
	text("gamemode", props.gamemode);
	number("view-distance", props.viewDistance);
	flag("white-list", props.whitelist);
	return props;
}

bool WriteServerProperties(const fs::path& serverDir, const ServerProperties& props, std::string* err)
{
	const std::map<std::string, std::string> values = {
		{ "server-port", std::to_string(props.port) },
		{ "server-ip", props.serverIp },
		{ "max-players", std::to_string(props.maxPlayers) },
		{ "motd", props.motd },
		{ "online-mode", props.onlineMode ? "true" : "false" },
		{ "pvp", props.pvp ? "true" : "false" },
		{ "difficulty", props.difficulty },
		{ "gamemode", props.gamemode },
		{ "view-distance", std::to_string(props.viewDistance) },
		{ "white-list", props.whitelist ? "true" : "false" },
	};
	const fs::path path = serverDir / "server.properties";
	std::vector<std::string> lines;
	{
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			lines.push_back(line);
		}
	}
	std::map<std::string, bool> written;
	for (std::string& line : lines) {
		const std::size_t eq = line.find('=');
		if (line.empty() || line[0] == '#' || eq == std::string::npos)
			continue;
		const auto it = values.find(line.substr(0, eq));
		if (it == values.end())
			continue;
		line = it->first + "=" + it->second;
		written[it->first] = true;
	}
	for (const auto& [key, value] : values) {
		if (!written.count(key))
			lines.push_back(key + "=" + value);
	}
	std::error_code ec;
	TracedFs::CreateDirectories(serverDir, ec);
	auto out = TracedFs::OpenWrite(path, std::ios::trunc | std::ios::binary);
	for (const std::string& line : lines)
		out << line << "\n";
	if (!out.good()) {
		if (err) *err = "Couldn't write " + path.string();
		return false;
	}
	return true;
}

std::string DockerImageTag(int javaMajor, const std::string& minecraftVersion)
{
	if (javaMajor <= 0) {
		// Mojang's requirements: 1.16 and older Java 8, 1.17-1.20.4 Java 17, 1.20.5-1.21.x
		// Java 21, and the year-numbered versions (26.1 and later) Java 25.
		int major = 0, minor = 0, patch = 0;
		std::sscanf(minecraftVersion.c_str(), "%d.%d.%d", &major, &minor, &patch);
		if (major >= 26)
			javaMajor = 25;
		else if (major == 1 && minor <= 16)
			javaMajor = 8;
		else if (major == 1 && (minor < 20 || (minor == 20 && patch <= 4)))
			javaMajor = 17;
		else
			javaMajor = 21;
	}
	if (javaMajor <= 8) return "java8";
	if (javaMajor <= 11) return "java11";
	if (javaMajor <= 17) return "java17";
	if (javaMajor <= 21) return "java21";
	return "java25";
}

std::string DockerComposeText(const std::string& packId, const ServerInfo& info, const ServerProperties& properties,
	int memoryMb, bool eulaAccepted)
{
	std::string container = "mclp-";
	for (const char c : packId) {
		const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		container += (std::isalnum(static_cast<unsigned char>(lower)) || lower == '-' || lower == '_' || lower == '.') ? lower : '-';
	}
	// The itzg image's TYPE and loader version variable for each loader.
	std::string type = "VANILLA", loaderVariable;
	if (info.loader == "Fabric") { type = "FABRIC"; loaderVariable = "FABRIC_LOADER_VERSION"; }
	else if (info.loader == "Quilt") { type = "QUILT"; loaderVariable = "QUILT_LOADER_VERSION"; }
	else if (info.loader == "Forge") { type = "FORGE"; loaderVariable = "FORGE_VERSION"; }
	else if (info.loader == "NeoForge") { type = "NEOFORGE"; loaderVariable = "NEOFORGE_VERSION"; }
	auto quoted = [](const std::string& value) {
		std::string out = "\"";
		for (const char c : value) {
			if (c == '"' || c == '\\')
				out += '\\';
			out += c;
		}
		return out + "\"";
	};
	const std::string port = std::to_string(properties.port);
	std::string text =
		"# Written by MC Launcher Premium. Runs this server folder with docker-minecraft-server:\n"
		"#   docker compose up -d    start it in the background\n"
		"#   docker compose logs -f  watch the console\n"
		"#   docker compose down     stop it (the world is saved first)\n"
		"# Don't start it in the launcher at the same time: both use this folder's world.\n"
		"# Options: https://docker-minecraft-server.readthedocs.io/\n"
		"services:\n"
		"  minecraft:\n"
		"    image: itzg/minecraft-server:" + DockerImageTag(info.javaMajor, info.minecraftVersion) + "\n"
		"    container_name: " + container + "\n"
		"    tty: true\n"
		"    stdin_open: true\n"
		"    ports:\n"
		"      - " + quoted(port + ":" + port) + "\n"
		"    environment:\n"
		"      EULA: " + quoted(eulaAccepted ? "TRUE" : "FALSE") + "\n"
		"      TYPE: " + quoted(type) + "\n"
		"      VERSION: " + quoted(info.minecraftVersion) + "\n";
	if (!loaderVariable.empty())
		text += "      " + loaderVariable + ": " + quoted(info.loaderVersion) + "\n";
	text +=
		"      MEMORY: " + quoted(std::to_string(memoryMb) + "M") + "\n"
		"      SKIP_SERVER_PROPERTIES: \"true\"\n"
		"    volumes:\n"
		"      - ./:/data\n"
		"    restart: unless-stopped\n";
	return text;
}

bool WriteDockerCompose(const fs::path& serverDir, const std::string& packId, const ServerInfo& info, int memoryMb,
	std::string* err)
{
	const ServerProperties properties = ReadServerProperties(serverDir);
	if (!properties.serverIp.empty()) {
		if (err) *err = "Set Host IP to \"All network adapters\" first. Inside a container the server can't listen on " +
			properties.serverIp + "; Docker publishes the port instead.";
		return false;
	}
	auto out = TracedFs::OpenWrite(serverDir / "docker-compose.yml", std::ios::trunc | std::ios::binary);
	out << DockerComposeText(packId, info, properties, memoryMb, EulaAccepted(serverDir));
	if (!out.good()) {
		if (err) *err = "Couldn't write " + (serverDir / "docker-compose.yml").string();
		return false;
	}
	return true;
}

bool EulaAccepted(const fs::path& serverDir)
{
	const auto values = ReadPropertiesFile(serverDir / "eula.txt");
	const auto eula = values.find("eula");
	return eula != values.end() && eula->second == "true";
}

bool ServerUsesDocker(const fs::path& serverDir)
{
	std::error_code ec;
	return fs::is_regular_file(serverDir / ".mclp-docker", ec);
}

bool SetServerUsesDocker(const fs::path& serverDir, bool docker, std::string* err)
{
	std::error_code ec;
	const fs::path marker = serverDir / ".mclp-docker";
	if (!docker) {
		TracedFs::Remove(marker, ec);
		if (ec && err) *err = "Couldn't remove " + marker.string() + ": " + ec.message();
		return !ec;
	}
	TracedFs::CreateDirectories(serverDir, ec);
	auto out = TracedFs::OpenWrite(marker, std::ios::trunc);
	out << "This server runs in Docker (docker-minecraft-server). Delete this file to run it with Java again.\n";
	if (!out.good()) {
		if (err) *err = "Couldn't write " + marker.string();
		return false;
	}
	return true;
}

bool AcceptEula(const fs::path& serverDir, std::string* err)
{
	std::error_code ec;
	TracedFs::CreateDirectories(serverDir, ec);
	auto out = TracedFs::OpenWrite(serverDir / "eula.txt", std::ios::trunc);
	out << "# Accepted in MC Launcher Premium (https://aka.ms/MinecraftEULA)\neula=true\n";
	if (!out.good()) {
		if (err) *err = "Couldn't write eula.txt";
		return false;
	}
	return true;
}

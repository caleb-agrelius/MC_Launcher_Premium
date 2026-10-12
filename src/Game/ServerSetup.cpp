#include "ServerSetup.h"

#include "Game/Loaders.h"
#include "Game/PackInstance.h"
#include "Network/HttpClient.h"
#include "Util/Process.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <map>
#include <set>
#include <regex>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char* kInfoFile = ".mclp-server.json";
static const char* kPreparedMarker = ".mclp-server-prepared";

bool CanCreateServer(const fs::path& extractedDir)
{
	std::error_code ec;
	return fs::is_regular_file(extractedDir / "modrinth.index.json", ec);
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
	std::map<fs::path, fs::path> files;
	for (const char* name : { "overrides", "server-overrides" }) {
		const fs::path source = extractedDir / name;
		if (!fs::is_directory(source))
			continue;
		for (const auto& entry : fs::recursive_directory_iterator(source)) {
			if (entry.is_regular_file())
				files[entry.path().lexically_relative(source)] = entry.path();
		}
	}
	for (const auto& [relative, source] : files) {
		const fs::path destination = target / relative;
		fs::create_directories(destination.parent_path(), ec);
		if (!ec)
			fs::copy_file(source, destination, fs::copy_options::skip_existing, ec);
		if (ec) {
			if (err) *err = "Couldn't copy " + relative.string() + ": " + ec.message();
			return false;
		}
	}
	return true;
}

// The vanilla server jar, from the version JSON (downloads.server).
static bool DownloadVanillaServer(HttpClient& http, const fs::path& gameRoot, const std::string& minecraftVersion,
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

// Finds Forge/NeoForge's Windows argument file (newer versions), relative to the server folder.
static std::string FindWindowsArgsFile(const fs::path& serverDir, const MrpackIndex& index)
{
	std::error_code ec;
	const bool modernNeoForge = index.loader == LoaderKind::NeoForge && index.minecraftVersion != "1.20.1";
	const fs::path base = index.loader == LoaderKind::Forge ? "libraries/net/minecraftforge/forge"
		: modernNeoForge ? "libraries/net/neoforged/neoforge" : "libraries/net/neoforged/forge";
	const std::string version = modernNeoForge ? index.loaderVersion : index.minecraftVersion + "-" + index.loaderVersion;
	const fs::path relative = base / version / "win_args.txt";
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

bool SetupServer(HttpClient& http, const ServerSetupRequest& request, ServerInfo& out, const ProgressFn& progress,
	std::string* err, const std::atomic<bool>* cancel)
{
	out = ServerInfo{};
	if (cancel && cancel->load()) {
		if (err) *err = "Cancelled";
		return false;
	}
	const fs::path& dir = request.serverDir;
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (ec) {
		if (err) *err = "Couldn't create the server folder: " + ec.message();
		return false;
	}

	// 1. The pack's server-side files and its overrides.
	if (progress)
		progress("Reading modpack", -1.0);
	MrpackIndex index;
	if (!ReadMrpackIndex(request.extractedDir, index, err, true))
		return false;
	std::vector<DownloadJob> jobs = index.files;
	for (DownloadJob& job : jobs)
		job.path = dir / job.path;
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
	std::set<std::string> currentFiles;
	for (const DownloadJob& job : index.files)
		currentFiles.insert(job.path.generic_string());
	if (prepared != current) {
		// Another pack version: remove mods the old one listed and this one doesn't.
		for (const std::string& old : previousFiles) {
			const fs::path relative = fs::u8path(old).lexically_normal();
			if (currentFiles.count(old) == 0 && !relative.empty() && relative.is_relative() && *relative.begin() != "..")
				fs::remove(dir / relative, ec);
		}
		if (progress)
			progress("Copying pack configuration", -1.0);
		if (!CopyOverrides(request.extractedDir, dir, err))
			return false;
		std::ofstream marker(dir / kPreparedMarker, std::ios::trunc);
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
			java = fs::is_directory(java / "bin", ec) ? java / "bin" / "java.exe" : java / "java.exe";
		if (java.filename() == "javaw.exe" && fs::is_regular_file(java.parent_path() / "java.exe", ec))
			java = java.parent_path() / "java.exe";
		if (!fs::is_regular_file(java, ec)) {
			if (err) *err = "The Java set in Settings > Minecraft wasn't found: " + request.javaPath;
			return false;
		}
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
		const std::string argsFile = FindWindowsArgsFile(dir, index);
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
	};
	const fs::path temporary = dir / (std::string(kInfoFile) + ".tmp");
	std::ofstream saved(temporary, std::ios::trunc);
	saved << info.dump(2) << "\n";
	saved.close();
	if (saved.good())
		fs::rename(temporary, dir / kInfoFile, ec);
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
	}
	catch (...) {
		return false;
	}
	return !out.java.empty() && !out.launchArgs.empty();
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
	fs::create_directories(serverDir, ec);
	std::ofstream out(path, std::ios::trunc | std::ios::binary);
	for (const std::string& line : lines)
		out << line << "\n";
	if (!out.good()) {
		if (err) *err = "Couldn't write " + path.string();
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

bool AcceptEula(const fs::path& serverDir, std::string* err)
{
	std::error_code ec;
	fs::create_directories(serverDir, ec);
	std::ofstream out(serverDir / "eula.txt", std::ios::trunc);
	out << "# Accepted in MC Launcher Premium (https://aka.ms/MinecraftEULA)\neula=true\n";
	if (!out.good()) {
		if (err) *err = "Couldn't write eula.txt";
		return false;
	}
	return true;
}

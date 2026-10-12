#include "Launcher.h"

#include "Game/Loaders.h"
#include "Game/PackInstance.h"
#include "Network/HttpClient.h"
#include "Util/Process.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

#ifdef _WIN32
static const char* kClasspathSeparator = ";";
#else
static const char* kClasspathSeparator = ":";
#endif

static std::string Substitute(const std::string& text, const std::map<std::string, std::string>& values)
{
	std::string out;
	std::size_t pos = 0;
	while (pos < text.size()) {
		const std::size_t start = text.find("${", pos);
		if (start == std::string::npos) {
			out += text.substr(pos);
			break;
		}
		const std::size_t end = text.find('}', start);
		if (end == std::string::npos) {
			out += text.substr(pos);
			break;
		}
		out += text.substr(pos, start - pos);
		const auto it = values.find(text.substr(start + 2, end - start - 2));
		if (it != values.end())
			out += it->second; // unknown placeholders become empty
		pos = end + 1;
	}
	return out;
}

// Expands an "arguments.jvm"/"arguments.game" array: plain strings, or
// { rules, value } objects whose value is a string or an array of strings.
static void ExpandArguments(const json& args, const std::map<std::string, std::string>& values, std::vector<std::string>& out)
{
	for (const auto& arg : args) {
		if (arg.is_string()) {
			out.push_back(Substitute(arg.get<std::string>(), values));
			continue;
		}
		if (!arg.is_object() || !RulesAllow(arg.value("rules", json())))
			continue;
		const json value = arg.value("value", json());
		if (value.is_string()) {
			out.push_back(Substitute(value.get<std::string>(), values));
		} else if (value.is_array()) {
			for (const auto& item : value) {
				if (item.is_string())
					out.push_back(Substitute(item.get<std::string>(), values));
			}
		}
	}
}

std::vector<std::string> SplitArguments(const std::string& text)
{
	std::vector<std::string> args;
	std::string current;
	bool quoted = false;
	bool any = false; // current holds an argument, even an empty quoted one
	for (char c : text) {
		if (c == '"') {
			quoted = !quoted;
			any = true;
		} else if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
			if (any)
				args.push_back(current);
			current.clear();
			any = false;
		} else {
			current += c;
			any = true;
		}
	}
	if (any)
		args.push_back(current);
	return args;
}

// The Java to run the game with: the user's choice from Settings, or Mojang's
// runtime. A console java.exe is swapped for its javaw.exe sibling so no
// console window opens.
static bool CustomJava(const std::string& setting, fs::path& javaw, fs::path& javaConsole, std::string* err)
{
	std::error_code ec;
	fs::path chosen = fs::path(setting);
	if (fs::is_directory(chosen, ec)) // a JDK/JRE folder or its bin/
		chosen = fs::is_directory(chosen / "bin", ec) ? chosen / "bin" / "javaw.exe" : chosen / "javaw.exe";
	if (!fs::is_regular_file(chosen, ec)) {
		if (err) *err = "The Java set in Settings > Minecraft wasn't found: " + setting;
		return false;
	}
	const fs::path dir = chosen.parent_path();
	const bool windows = chosen.extension() == ".exe";
	javaw = windows && fs::is_regular_file(dir / "javaw.exe", ec) ? dir / "javaw.exe" : chosen;
	javaConsole = windows && fs::is_regular_file(dir / "java.exe", ec) ? dir / "java.exe" : chosen;
	return true;
}

static std::string PathString(const fs::path& path)
{
	return path.lexically_normal().string();
}

// Java 9+ reads arguments from an @file, which keeps long classpaths under
// Windows' 32K command-line limit. Backslashes and quotes are escaped.
static bool WriteArgFile(const fs::path& path, const std::vector<std::string>& args, std::string* err)
{
	std::ofstream ofs(path, std::ios::trunc);
	for (const std::string& arg : args) {
		ofs << '"';
		for (char c : arg) {
			if (c == '\\' || c == '"')
				ofs << '\\';
			ofs << c;
		}
		ofs << "\"\n";
	}
	if (!ofs.good()) {
		if (err) *err = "Couldn't write " + path.string();
		return false;
	}
	return true;
}

static bool LaunchVersion(const PlayRequest& request, const ResolvedVersion& version, const fs::path& javaExe,
	const ProgressFn& progress, std::string* err, const std::atomic<bool>* cancel)
{
	const fs::path& root = request.gameRoot;
	const Account& account = request.account;

	std::string classpath;
	for (const LibraryFile& file : version.classpath) {
		classpath += PathString(file.path);
		classpath += kClasspathSeparator;
	}
	classpath += PathString(ClientJarPath(root, version));

	const std::string assetIndexId = version.assetIndex.is_null() ? std::string() : version.assetIndex.value("id", "");
	fs::path gameAssets = root / "assets";
	std::error_code ec;
	if (fs::is_directory(root / "assets" / "virtual" / assetIndexId, ec))
		gameAssets = root / "assets" / "virtual" / assetIndexId;

	const std::map<std::string, std::string> values = {
		{ "auth_player_name", account.name },
		{ "auth_uuid", account.uuid },
		{ "auth_access_token", account.accessToken },
		{ "auth_session", "token:" + account.accessToken + ":" + account.uuid },
		{ "auth_xuid", account.xuid.empty() ? "0" : account.xuid },
		{ "clientid", "" },
		{ "user_type", "msa" },
		{ "user_properties", "{}" },
		{ "version_name", version.id },
		{ "version_type", version.type.empty() ? "release" : version.type },
		{ "game_directory", PathString(request.instanceDir) },
		{ "assets_root", PathString(root / "assets") },
		{ "game_assets", PathString(gameAssets) },
		{ "assets_index_name", assetIndexId },
		{ "natives_directory", PathString(NativesDir(root, version)) },
		{ "library_directory", PathString(root / "libraries") },
		{ "classpath", classpath },
		{ "classpath_separator", kClasspathSeparator },
		{ "launcher_name", "MC_Launcher_Premium" },
		{ "launcher_version", "0.1" },
	};

	const LaunchSettings& launch = request.launch;
	std::vector<std::string> jvmArgs;
	if (launch.minMemoryMb > 0)
		jvmArgs.push_back("-Xms" + std::to_string(std::min(launch.minMemoryMb, launch.maxMemoryMb)) + "M");
	jvmArgs.push_back("-Xmx" + std::to_string(launch.maxMemoryMb) + "M");
	if (!version.jvmArgs.empty()) {
		ExpandArguments(version.jvmArgs, values, jvmArgs);
	} else {
		jvmArgs.push_back("-Djava.library.path=" + values.at("natives_directory"));
		jvmArgs.push_back("-cp");
		jvmArgs.push_back(classpath);
	}
	if (!version.logging.is_null() && version.logging.contains("file")) {
		const fs::path config = root / "assets" / "log_configs" / version.logging["file"].value("id", "");
		if (fs::is_regular_file(config, ec))
			jvmArgs.push_back(Substitute(version.logging.value("argument", ""), { { "path", PathString(config) } }));
	}
	// The player's own JVM arguments go last, so they win over the defaults above.
	for (std::string& arg : SplitArguments(launch.jvmArgs))
		jvmArgs.push_back(std::move(arg));

	std::vector<std::string> gameArgs;
	if (!version.gameArgs.empty()) {
		ExpandArguments(version.gameArgs, values, gameArgs);
	} else {
		std::istringstream words(version.legacyGameArgs);
		std::string word;
		while (words >> word)
			gameArgs.push_back(Substitute(word, values));
	}
	// Window size from Settings. The version's own resolution arguments sit behind a
	// feature rule that never matches, so these aren't duplicated.
	if (launch.fullscreen) {
		gameArgs.push_back("--fullscreen");
	} else if (launch.windowWidth > 0 && launch.windowHeight > 0) {
		gameArgs.push_back("--width");
		gameArgs.push_back(std::to_string(launch.windowWidth));
		gameArgs.push_back("--height");
		gameArgs.push_back(std::to_string(launch.windowHeight));
	}

	std::vector<std::string> command = { javaExe.string() };
	if (version.javaMajor >= 9) {
		// The access token stays on the command line, not in the file.
		const fs::path argFile = request.instanceDir / ".mclp-jvm-args.txt";
		if (!WriteArgFile(argFile, jvmArgs, err))
			return false;
		command.push_back("@" + argFile.string());
	} else {
		command.insert(command.end(), jvmArgs.begin(), jvmArgs.end());
	}
	command.push_back(version.mainClass);
	command.insert(command.end(), gameArgs.begin(), gameArgs.end());

	if (progress)
		progress("Playing", 1.0);
	const fs::path log = request.instanceDir / "logs" / "launcher-output.log";
	int exitCode = 0;
	if (!RunProcess(command, request.instanceDir, log, true, &exitCode, err, cancel, false))
		return false;
	if (exitCode != 0) {
		if (err) *err = "Minecraft exited with code " + std::to_string(exitCode) + ". See " + PathString(log) +
			" and the instance's logs/latest.log.";
		return false;
	}
	return true;
}

bool PlayModpack(HttpClient& http, const PlayRequest& request, const ProgressFn& progress, std::string* err,
	const std::atomic<bool>* cancel)
{
	const fs::path& root = request.gameRoot;
	if (progress)
		progress("Reading modpack", -1.0);
	MrpackIndex index;
	if (!ReadMrpackIndex(request.extractedDir, index, err))
		return false;
	if (!PrepareInstance(http, request.extractedDir, request.instanceDir, index, progress, err, cancel))
		return false;

	if (progress)
		progress("Preparing Minecraft " + index.minecraftVersion, -1.0);
	if (!InstallVanillaJson(http, root, index.minecraftVersion, err, cancel))
		return false;
	ResolvedVersion vanilla;
	if (!ResolveVersion(root, index.minecraftVersion, vanilla, err))
		return false;

	fs::path javaw;
	fs::path javaConsole;
	if (!request.launch.javaPath.empty()) {
		if (!CustomJava(request.launch.javaPath, javaw, javaConsole, err))
			return false;
	} else {
		if (!InstallJavaRuntime(http, root, vanilla.javaComponent, true, javaw, progress, err, cancel))
			return false;
		javaConsole = javaw.parent_path() / (javaw.extension() == ".exe" ? "java.exe" : "java");
	}

	if (!InstallGameFiles(http, root, vanilla, progress, err, cancel))
		return false;

	std::string versionId;
	if (!InstallLoader(http, root, index.loader, index.minecraftVersion, index.loaderVersion, javaConsole,
			versionId, progress, err, cancel))
		return false;
	ResolvedVersion version;
	if (!ResolveVersion(root, versionId, version, err))
		return false;
	if (versionId != vanilla.id && !InstallGameFiles(http, root, version, progress, err, cancel))
		return false;

	if (cancel && cancel->load()) {
		if (err) *err = "Cancelled";
		return false;
	}
	if (progress)
		progress("Starting Minecraft", -1.0);
	return LaunchVersion(request, version, javaw, progress, err, cancel);
}

#include "Launcher.h"

#include "Game/GameOptions.h"
#include "Game/InstanceContent.h"
#include "Game/JarMods.h"
#include "Game/JavaFinder.h"
#include "Game/Loaders.h"
#include "Game/PackInstance.h"
#include "Network/HttpClient.h"
#include "Util/Process.h"
#include "Util/TracedFs.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
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
		if (err) *err = "The Java chosen in the launch settings wasn't found: " + setting;
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
	auto ofs = TracedFs::OpenWrite(path, std::ios::trunc);
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

// Versions before 1.6 read assets by name from <game dir>/resources/ (their asset
// index sets "map_to_resources"). Copies missing ones there from the object store.
static void MapAssetsToResources(const fs::path& root, const ResolvedVersion& version, const fs::path& gameDir)
{
	if (version.assetIndex.is_null())
		return;
	const fs::path indexPath = root / "assets" / "indexes" / (version.assetIndex.value("id", "") + ".json");
	json index;
	try {
		std::ifstream ifs(indexPath);
		if (!ifs.is_open())
			return;
		ifs >> index;
	}
	catch (...) {
		return;
	}
	if (!index.value("map_to_resources", false))
		return;
	const json objects = index.value("objects", json::object());
	for (const auto& [name, object] : objects.items()) {
		const std::string hash = object.value("hash", "");
		const fs::path relative = fs::u8path(name).lexically_normal();
		if (hash.size() < 2 || relative.empty() || relative.is_absolute() || *relative.begin() == "..")
			continue;
		const fs::path target = gameDir / "resources" / relative;
		std::error_code ec;
		if (fs::exists(target, ec))
			continue;
		TracedFs::CreateDirectories(target.parent_path(), ec);
		TracedFs::CopyFile(root / "assets" / "objects" / hash.substr(0, 2) / hash, target, ec);
	}
}

// clientJar is the game jar to put on the classpath: the version's own, or a copy with jar
// mods patched in.
static bool LaunchVersion(const PlayRequest& request, const ResolvedVersion& version, const fs::path& javaExe,
	const fs::path& clientJar, const ProgressFn& progress, std::string* err, const std::atomic<bool>* cancel)
{
	MapAssetsToResources(request.gameRoot, version, request.instanceDir);
	const fs::path& root = request.gameRoot;
	const Account& account = request.account;

	std::string classpath;
	for (const LibraryFile& file : version.classpath) {
		classpath += PathString(file.path);
		classpath += kClasspathSeparator;
	}
	classpath += PathString(clientJar);

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
	// Old Forge checks Mojang's signature on the game jar, which current Java 8 no longer
	// accepts (SHA-1 signed), and refuses to start. Prism and MultiMC always pass these;
	// versions that don't read them ignore them.
	jvmArgs.push_back("-Dfml.ignoreInvalidMinecraftCertificates=true");
	jvmArgs.push_back("-Dfml.ignorePatchDiscrepancies=true");
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
	if (!RunProcess(command, request.instanceDir, log, true, &exitCode, err, cancel, false, request.stopGame))
		return false;
	if (exitCode != 0) {
		if (err) *err = "Minecraft exited with code " + std::to_string(exitCode) + ". See " + PathString(log) +
			" and the instance's logs/latest.log.";
		return false;
	}
	return true;
}

// Installs vanilla Minecraft, Java and the loader into the shared game root, then
// launches. Shared by .mrpack instances and whole-pack installs.
// loaderJar: a Forge universal jar the pack ships (or empty); old Forge is installed from it.
static bool InstallAndLaunch(IHttpClient& http, const PlayRequest& request, const std::string& minecraftVersion,
	LoaderKind loader, const std::string& loaderVersion, const fs::path& loaderJar, const ProgressFn& progress,
	std::string* err, const std::atomic<bool>* cancel)
{
	const fs::path& root = request.gameRoot;
	// After the pack's own files, so the player's defaults from Settings win.
	if (!request.gameOptions.values.empty()) {
		if (progress)
			progress("Applying game options", -1.0);
		if (!ApplyGameOptions(request.instanceDir, request.gameOptions, err))
			return false;
	}

	if (progress)
		progress("Preparing Minecraft " + minecraftVersion, -1.0);
	if (!InstallVanillaJson(http, root, minecraftVersion, err, cancel))
		return false;
	ResolvedVersion vanilla;
	if (!ResolveVersion(root, minecraftVersion, vanilla, err))
		return false;

	fs::path javaw;
	fs::path javaConsole;
	if (!request.launch.javaPath.empty()) {
		if (!CustomJava(request.launch.javaPath, javaw, javaConsole, err))
			return false;
	} else if (const std::vector<JavaInstall> installs = FindJavaInstalls(root);
			const JavaInstall* installed = PickJava(installs, vanilla.javaMajor)) {
		// Automatic: an installed Java with the major version this Minecraft needs.
		javaw = installed->javaw;
		javaConsole = installed->java;
		if (progress)
			progress("Using " + JavaLabel(*installed), -1.0);
	} else {
		// None installed: Mojang's runtime for this version, downloaded once.
		if (!InstallJavaRuntime(http, root, vanilla.javaComponent, true, javaw, progress, err, cancel))
			return false;
		javaConsole = javaw.parent_path() / (javaw.extension() == ".exe" ? "java.exe" : "java");
	}

	if (!InstallGameFiles(http, root, vanilla, progress, err, cancel))
		return false;

	std::string versionId;
	if (loader == LoaderKind::Forge && !loaderJar.empty() && IsLegacyForgeMinecraft(minecraftVersion)) {
		// Forge before 1.13 from the pack's own copy: its old installers can't run headless.
		if (progress)
			progress("Installing Forge " + loaderVersion + " from the pack", -1.0);
		if (!InstallForgeFromJar(root, loaderJar, minecraftVersion, versionId, err))
			return false;
		if (NeedsLegacyJavaFixer(minecraftVersion, loaderVersion) &&
			!DownloadAll(http, { LegacyJavaFixerJob(request.instanceDir) }, "Adding LegacyJavaFixer for Java 8", progress, err, cancel))
			return false;
	} else if (!InstallLoader(http, root, loader, minecraftVersion, loaderVersion, javaConsole,
			versionId, progress, err, cancel)) {
		return false;
	}
	ResolvedVersion version;
	if (!ResolveVersion(root, versionId, version, err))
		return false;
	if (versionId != vanilla.id && !InstallGameFiles(http, root, version, progress, err, cancel))
		return false;

	if (cancel && cancel->load()) {
		if (err) *err = "Cancelled";
		return false;
	}
	// Jar mods (very old packs) are patched into a copy of the game jar in the game folder.
	fs::path clientJar = ClientJarPath(root, version);
	const std::vector<fs::path> jarMods = FindJarMods(request.extractedDir, request.instanceDir);
	if (!jarMods.empty()) {
		if (progress)
			progress("Patching " + std::to_string(jarMods.size()) + " jar mod(s) into the game", -1.0);
		const fs::path patched = request.instanceDir / ".mclp" / "patched-client.jar";
		if (!BuildPatchedJar(clientJar, jarMods, patched, err))
			return false;
		clientJar = patched;
	}

	if (progress)
		progress("Starting Minecraft", -1.0);
	return LaunchVersion(request, version, javaw, clientJar, progress, err, cancel);
}

// Drops the pack files the player removed from the index, so they aren't downloaded again.
static void SkipRemovedContent(MrpackIndex& index, const ContentChanges& changes)
{
	const std::set<std::string> removed = RemovedContent(changes);
	if (removed.empty())
		return;
	index.files.erase(std::remove_if(index.files.begin(), index.files.end(),
		[&](const DownloadJob& file) { return removed.count(file.path.generic_u8string()) > 0; }), index.files.end());
}

bool RepairModpack(IHttpClient& http, const PlayRequest& request, const ProgressFn& progress, std::string* err,
	const std::atomic<bool>* cancel)
{
	if (progress)
		progress("Reading modpack", -1.0);
	MrpackIndex index;
	if (!ReadMrpackIndex(request.extractedDir, index, err))
		return false;
	SkipRemovedContent(index, request.contentChanges);
	if (!RepairInstance(http, request.extractedDir, request.instanceDir, index, progress, err, cancel))
		return false;
	ApplyContentChanges(request.instanceDir, request.contentChanges);
	return true;
}

bool PlayModpack(IHttpClient& http, const PlayRequest& request, const ProgressFn& progress, std::string* err,
	const std::atomic<bool>* cancel)
{
	// No launching without a verified Microsoft account (there's no offline play).
	if (!CanLaunchAs(request.account)) {
		if (err) *err = "Sign in with a Microsoft account that owns Minecraft: Java Edition first.";
		return false;
	}
	if (progress)
		progress("Reading modpack", -1.0);

	// Whole-pack installs (CurseForge, FTB, Technic, ATLauncher) are already a game folder.
	if (IsWholePackInstall(request.extractedDir)) {
		InstalledPack pack;
		if (!ReadInstalledPack(request.extractedDir, pack, err))
			return false;
		std::error_code ec;
		if (!fs::is_directory(request.instanceDir, ec)) {
			if (err) *err = "The pack's game folder is missing: " + PathString(request.instanceDir) + ". Install it again.";
			return false;
		}
		return InstallAndLaunch(http, request, pack.minecraftVersion, pack.loader, pack.loaderVersion, pack.loaderJar, progress, err, cancel);
	}

	MrpackIndex index;
	if (!ReadMrpackIndex(request.extractedDir, index, err))
		return false;
	SkipRemovedContent(index, request.contentChanges);
	if (!PrepareInstance(http, request.extractedDir, request.instanceDir, index, progress, err, cancel))
		return false;
	// A new pack version's overrides can bring back files the player removed or switched.
	ApplyContentChanges(request.instanceDir, request.contentChanges);
	return InstallAndLaunch(http, request, index.minecraftVersion, index.loader, index.loaderVersion, {}, progress, err, cancel);
}

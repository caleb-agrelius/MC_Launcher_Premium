#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class HttpClient;

// Minecraft game files shared by every instance, stored under one root folder
// (minecraft/ in the data folder) in the official launcher's layout:
// versions/<id>/<id>.json|.jar, libraries/, assets/, runtime/<java component>/.

// Reports a step description and its progress (0-1, or negative when unknown).
using ProgressFn = std::function<void(const std::string& step, double fraction)>;

struct DownloadJob {
	std::string url;
	std::filesystem::path path;
	std::string sha1;        // checked after download when not empty
	std::uint64_t size = 0;  // an existing file of this size is kept; 0 = keep any existing file
	bool executable = false; // unused on Windows
};

// Downloads every job that's missing, using several threads. Each file is
// written to <path>.part, checked against its SHA-1, then renamed into place.
bool DownloadAll(HttpClient& http,
				 const std::vector<DownloadJob>& jobs,
				 const std::string& step,
				 const ProgressFn& progress,
				 std::string* err,
				 const std::atomic<bool>* cancel);

struct LibraryFile {
	std::string key;            // group:artifact[:classifier], for de-duplication
	std::filesystem::path path; // absolute path under libraries/
	std::string url;            // empty if another tool (Forge's installer) creates it
	std::string sha1;
	std::uint64_t size = 0;
};

// A version JSON with its inheritsFrom chain merged, and libraries resolved for this OS.
struct ResolvedVersion {
	std::string id;          // the version launched (e.g. fabric-loader-0.16.5-1.21.1)
	std::string jarId;       // the version whose client jar is used (the vanilla one)
	std::string type;        // release, snapshot, ...
	std::string mainClass;
	nlohmann::json jvmArgs = nlohmann::json::array();  // "arguments.jvm" entries
	nlohmann::json gameArgs = nlohmann::json::array(); // "arguments.game" entries
	std::string legacyGameArgs; // "minecraftArguments" (pre-1.13 versions)
	nlohmann::json assetIndex;  // { id, url, sha1, size }
	nlohmann::json clientDownload; // downloads.client { url, sha1, size }
	nlohmann::json logging;     // logging.client, may be null
	std::string javaComponent = "jre-legacy";
	int javaMajor = 8;
	std::vector<LibraryFile> classpath; // in order, without duplicates
	std::vector<LibraryFile> natives;   // old-style native jars to extract
};

// Makes sure versions/<id>/<id>.json exists for a vanilla Minecraft version,
// downloading it from Mojang's version manifest if needed.
bool InstallVanillaJson(HttpClient& http,
						const std::filesystem::path& root,
						const std::string& minecraftVersion,
						std::string* err,
						const std::atomic<bool>* cancel);

// Reads versions/<id>/<id>.json and every version it inherits from (all must
// already be on disk) and merges them.
bool ResolveVersion(const std::filesystem::path& root,
					const std::string& versionId,
					ResolvedVersion& out,
					std::string* err);

// Downloads the client jar, libraries, assets and logging config, and extracts
// old-style natives into versions/<id>/natives.
bool InstallGameFiles(HttpClient& http,
					  const std::filesystem::path& root,
					  const ResolvedVersion& version,
					  const ProgressFn& progress,
					  std::string* err,
					  const std::atomic<bool>* cancel);

// Installs one of Mojang's Java runtimes (e.g. "java-runtime-delta") under
// runtime/<component>/ and returns the path to its java executable.
// windowed = true returns javaw.exe (no console) on Windows.
bool InstallJavaRuntime(HttpClient& http,
						const std::filesystem::path& root,
						const std::string& component,
						bool windowed,
						std::filesystem::path& javaExe,
						const ProgressFn& progress,
						std::string* err,
						const std::atomic<bool>* cancel);

// Evaluates a Mojang "rules" array for this OS. Feature rules (demo user,
// custom resolution, quick play) never match: the launcher uses none of them.
bool RulesAllow(const nlohmann::json& rules);

// Path of a maven coordinate ("group:artifact:version[:classifier][@ext]")
// relative to a repository root, e.g. net/fabricmc/fabric-loader/0.16.5/fabric-loader-0.16.5.jar.
std::string MavenPath(const std::string& coordinate);

// Where the client jar and natives for a version live.
std::filesystem::path ClientJarPath(const std::filesystem::path& root, const ResolvedVersion& version);
std::filesystem::path NativesDir(const std::filesystem::path& root, const ResolvedVersion& version);

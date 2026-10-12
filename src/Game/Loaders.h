#pragma once

#include "Game/GameFiles.h"

#include <atomic>
#include <filesystem>
#include <string>

class HttpClient;

enum class LoaderKind { Vanilla, Fabric, Quilt, Forge, NeoForge };

const char* LoaderName(LoaderKind kind);

// Downloads the official Forge or NeoForge installer jar into root/installers/
// (kept, so it isn't downloaded again) and sets installer to its path.
bool DownloadForgeInstaller(HttpClient& http,
							const std::filesystem::path& root,
							LoaderKind kind,
							const std::string& minecraftVersion,
							const std::string& loaderVersion,
							std::filesystem::path& installer,
							const ProgressFn& progress,
							std::string* err,
							const std::atomic<bool>* cancel);

// Installs a mod loader's version JSON (and, for Forge/NeoForge, the files its
// installer generates) for a Minecraft version whose vanilla files are already
// installed. Sets versionId to the version to launch.
// Fabric and Quilt come from their meta APIs. Forge and NeoForge run their
// official installer headless with javaExe (a console java, not javaw).
bool InstallLoader(HttpClient& http,
				   const std::filesystem::path& root,
				   LoaderKind kind,
				   const std::string& minecraftVersion,
				   const std::string& loaderVersion,
				   const std::filesystem::path& javaExe,
				   std::string& versionId,
				   const ProgressFn& progress,
				   std::string* err,
				   const std::atomic<bool>* cancel);

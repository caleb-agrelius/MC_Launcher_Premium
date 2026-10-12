#pragma once

#include "Game/GameFiles.h"

#include <atomic>
#include <filesystem>
#include <string>

class IHttpClient;

enum class LoaderKind { Vanilla, Fabric, Quilt, Forge, NeoForge };

const char* LoaderName(LoaderKind kind);

// Downloads the official Forge or NeoForge installer jar into root/installers/
// (kept, so it isn't downloaded again) and sets installer to its path.
bool DownloadForgeInstaller(IHttpClient& http,
							const std::filesystem::path& root,
							LoaderKind kind,
							const std::string& minecraftVersion,
							const std::string& loaderVersion,
							std::filesystem::path& installer,
							const ProgressFn& progress,
							std::string* err,
							const std::atomic<bool>* cancel);

// Installs Forge from its own universal jar (Minecraft 1.6 to 1.12, where Forge's old
// installers can't run headless): writes the jar's version.json as versions/<id>/<id>.json,
// inheriting from vanilla, and copies the jar into libraries/ as the Forge library. Library
// URLs on the retired files.minecraftforge.net are moved to maven.minecraftforge.net, and
// server-only libraries are dropped. Sets versionId to the version to launch.
bool InstallForgeFromJar(const std::filesystem::path& root,
						 const std::filesystem::path& forgeJar,
						 const std::string& minecraftVersion,
						 std::string& versionId,
						 std::string* err);

// True for Minecraft versions before 1.13, whose Forge can be installed from its universal jar.
bool IsLegacyForgeMinecraft(const std::string& minecraftVersion);

// Forge for Minecraft 1.6 and 1.7 before 10.13.2.1230 crashes on Java 8 while sorting its
// tweakers (a ConcurrentModificationException in LaunchWrapper). LegacyJavaFixer, a small
// coremod from Forge's own maven, fixes it; Mojang has no older Java for these versions.
bool NeedsLegacyJavaFixer(const std::string& minecraftVersion, const std::string& forgeVersion);
// The LegacyJavaFixer download, to be placed in the game folder's mods/.
DownloadJob LegacyJavaFixerJob(const std::filesystem::path& gameDir);

// Installs a mod loader's version JSON (and, for Forge/NeoForge, the files its
// installer generates) for a Minecraft version whose vanilla files are already
// installed. Sets versionId to the version to launch.
// Fabric and Quilt come from their meta APIs. Forge and NeoForge run their
// official installer headless with javaExe (a console java, not javaw).
bool InstallLoader(IHttpClient& http,
				   const std::filesystem::path& root,
				   LoaderKind kind,
				   const std::string& minecraftVersion,
				   const std::string& loaderVersion,
				   const std::filesystem::path& javaExe,
				   std::string& versionId,
				   const ProgressFn& progress,
				   std::string* err,
				   const std::atomic<bool>* cancel);

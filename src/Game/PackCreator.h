#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

class IHttpClient;

// New packs the player builds themselves (My Library > New pack...): a Minecraft version,
// a mod loader, and content added afterwards from Manage > Content.

// Minecraft release versions, newest first (Mojang's version manifest).
bool FetchMinecraftReleases(IHttpClient& http, std::vector<std::string>& out, std::string* err = nullptr,
							const std::atomic<bool>* cancel = nullptr);

// The versions of loader ("fabric", "quilt", "forge" or "neoforge") made for minecraftVersion,
// newest (or recommended) first, in the form pack.json and InstallLoader use (e.g. "0.16.9",
// "47.2.0", "21.1.77"). Empty when the loader has none for that version.
bool FetchLoaderVersions(IHttpClient& http, const std::string& loader, const std::string& minecraftVersion,
						 std::vector<std::string>& out, std::string* err = nullptr,
						 const std::atomic<bool>* cancel = nullptr);

// The NeoForge version prefix for a Minecraft version: "1.21.1" -> "21.1.", "1.21" -> "21.0.",
// and the year-based "26.1" -> "26.1.".
std::string NeoForgeVersionPrefix(const std::string& minecraftVersion);

// A pack id for name that isn't in taken: "custom-" plus the name in lowercase letters,
// digits and dashes, with "-2", "-3", ... added when needed.
std::string CustomPackId(const std::string& name, const std::vector<std::string>& taken);

// Writes a whole-pack install for a new pack: installDir/pack.json (source "custom", the
// version, Minecraft version and loader) and empty installDir/minecraft/{mods,resourcepacks,
// shaderpacks}. loader is empty for vanilla Minecraft.
bool WriteCustomPack(const std::filesystem::path& installDir, const std::string& version, const std::string& minecraftVersion,
					 const std::string& loader, const std::string& loaderVersion, std::string* err);

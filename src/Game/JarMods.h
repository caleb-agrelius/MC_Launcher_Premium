#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Jar mods: zips whose classes replace the game's own, the way mods worked before Forge
// became a separate loader (Minecraft 1.5 and earlier). ATLauncher packs list them as
// "jar" and "forge" mods, saved to <game folder>/jarmods/.

// The jar mods in a game folder's jarmods/, in install order: the order pack.json's
// "files" lists them in, then any others by name. Empty if there are none. Jars with a
// version.json are left out: those are a loader (Forge 1.6 and later), installed as one.
std::vector<std::filesystem::path> FindJarMods(const std::filesystem::path& installDir,
											   const std::filesystem::path& gameDir);

// Writes output: the client jar with each jar mod's entries layered on top, later mods
// winning, and META-INF/ left out (its signatures would no longer match). Skips the work
// when output was already built from the same inputs (recorded next to it in
// <output>.inputs). False with err if a zip can't be read or the jar can't be written.
bool BuildPatchedJar(const std::filesystem::path& clientJar,
					 const std::vector<std::filesystem::path>& jarMods,
					 const std::filesystem::path& output,
					 std::string* err);

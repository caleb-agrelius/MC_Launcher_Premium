#pragma once

#include "Game/GameFiles.h"
#include "Storage/Account.h"
#include "Storage/Settings.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

class HttpClient;

struct PlayRequest {
	std::filesystem::path gameRoot;     // shared Minecraft files (minecraft/)
	std::filesystem::path extractedDir; // installs/<pack id>/ (the unzipped .mrpack)
	std::filesystem::path instanceDir;  // instances/<pack id>/ (the game directory)
	Account account;                    // must have a valid access token
	LaunchSettings launch;              // memory, Java, JVM arguments, window size
};

// Splits a JVM argument string on spaces; double quotes group words ("-Dname=a b").
std::vector<std::string> SplitArguments(const std::string& text);

// Everything needed to play a modpack, in order: mods and overrides, vanilla
// Minecraft, Java, the mod loader, then the game itself. Blocks until Minecraft
// exits. When cancel is set before launch it stops; after launch the game keeps
// running and PlayModpack returns false with err = "Cancelled".
// progress receives "Playing" (fraction 1) once the game has started.
bool PlayModpack(HttpClient& http,
				 const PlayRequest& request,
				 const ProgressFn& progress,
				 std::string* err,
				 const std::atomic<bool>* cancel);

#pragma once

#include "Game/GameFiles.h"
#include "Storage/Account.h"
#include "Storage/Settings.h"

#include <atomic>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

class IHttpClient;

struct PlayRequest {
	std::filesystem::path gameRoot;     // shared Minecraft files (minecraft/)
	std::filesystem::path extractedDir; // installs/<pack id>/ (the unzipped .mrpack)
	std::filesystem::path instanceDir;  // instances/<pack id>/ (the game directory)
	Account account;                    // must have a valid access token
	LaunchSettings launch;              // memory, Java, JVM arguments, window size
	GameOptionsSettings gameOptions;    // vanilla options.txt values applied to the instance
	const std::atomic<bool>* stopGame = nullptr; // set to force-close the running game
	// The player's edits to the pack version (InstalledVersion::contentChanges): removed
	// files aren't downloaded again, and the edits are re-applied after the pack's files.
	std::map<std::string, std::string> contentChanges;
};

// Splits a JVM argument string on spaces; double quotes group words ("-Dname=a b").
std::vector<std::string> SplitArguments(const std::string& text);

// Re-checks an .mrpack instance's files against their hashes and re-downloads damaged or
// missing ones (see RepairInstance). Doesn't launch.
bool RepairModpack(IHttpClient& http,
				   const PlayRequest& request,
				   const ProgressFn& progress,
				   std::string* err,
				   const std::atomic<bool>* cancel);

// Everything needed to play a modpack, in order: mods and overrides, game options,
// vanilla Minecraft, Java, the mod loader, then the game itself. Blocks until
// Minecraft exits. When cancel is set before launch it stops; after launch the game
// keeps running and PlayModpack returns false with err = "Cancelled". When
// request.stopGame is set while the game runs, the game is killed and
// PlayModpack returns false with err = "Stopped".
// progress receives "Playing" (fraction 1) once the game has started.
bool PlayModpack(IHttpClient& http,
				 const PlayRequest& request,
				 const ProgressFn& progress,
				 std::string* err,
				 const std::atomic<bool>* cancel);

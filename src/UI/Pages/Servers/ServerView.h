#pragma once

#include "Game/ServerSetup.h"
#include "Model/Modpack.h"
#include "Platform/Platform.h"
#include "Services/Servers/ServerState.h"
#include "UI/LogView.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// What the Servers page remembers about one server's screen, apart from the server itself:
// the open tab, the console's scroll and selection, half-typed fields. Starts over when the
// server is deleted.
struct ServerView {
	unsigned generation = 0;   // ServerState::generation this view belongs to
	int tab = 0;               // Run, Settings, Mods, Docker
	char command[256] = "";    // the console's input line
	LogView console;           // selectable, copyable console output
	std::vector<std::string> consoleLines; // copied from the process when it has new lines
	std::uint64_t consoleTotal = ~0ULL;    // the process's line count when they were copied
	bool hostIpCustom = false; // Host IP: typing an address instead of picking one
	std::string hostIpEdit;    // the address being typed, applied once it's valid
	std::string modFilter;
	std::string modError;

	// A freshly started process has a new console.
	void ResetConsole()
	{
		consoleTotal = ~0ULL;
		consoleLines.clear();
		console.ScrollToBottom();
		console.ClearSelection();
	}
};

// This PC's addresses, read each time the Servers page opens.
struct HostNetwork {
	std::string lanAddress;
	std::vector<Platform::NetworkAddress> adapters;
};

// Everything a Servers tab needs to know about the server on screen.
struct ServerTabContext {
	const std::string& packId;
	const Modpack& pack;
	ServerState& server;
	ServerView& view;
	const ServerSetupSnapshot& setup;
	const std::filesystem::path& dir; // servers/<pack id>
	const HostNetwork& network;
	DockerStatus docker;
	bool inLibrary;     // the pack is still in My Library
	bool supported;     // the pack can be (re)built into a server; a finished server can still run after its pack was uninstalled
	bool hasServerDir;  // servers/<pack id> exists
};

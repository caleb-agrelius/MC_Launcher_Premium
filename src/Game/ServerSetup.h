#pragma once

#include "Game/GameFiles.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

class HttpClient;

// One-click dedicated servers for installed Modrinth packs: the pack's server-side
// files, its mod loader's server, and a Java to run it, in servers/<pack id>/.

struct ServerSetupRequest {
	std::filesystem::path gameRoot;     // minecraft/ (version JSONs, Java runtimes, installers)
	std::filesystem::path extractedDir; // installs/<pack id>/ (the unzipped .mrpack)
	std::filesystem::path serverDir;    // servers/<pack id>/
	std::string javaPath;               // Settings > Minecraft's Java, or empty for Mojang's runtime
};

// What SetupServer installed, saved as serverDir/.mclp-server.json.
struct ServerInfo {
	std::string minecraftVersion;
	std::string loader;        // "Vanilla", "Fabric", "Quilt", "Forge", "NeoForge"
	std::string loaderVersion;
	std::string packVersion;
	std::string java;          // absolute path to java(.exe)
	std::vector<std::string> launchArgs; // after the JVM memory flags, e.g. { "-jar", "server.jar", "nogui" }
};

// True for packs that can be turned into a server (they have a modrinth.index.json).
bool CanCreateServer(const std::filesystem::path& extractedDir);

// Downloads and installs everything the server needs. Safe to run again to update the
// server after the pack was reinstalled; world, configs and server.properties are kept.
bool SetupServer(HttpClient& http,
				 const ServerSetupRequest& request,
				 ServerInfo& out,
				 const ProgressFn& progress,
				 std::string* err,
				 const std::atomic<bool>* cancel);

// Reads .mclp-server.json. False if the server hasn't been set up.
bool LoadServerInfo(const std::filesystem::path& serverDir, ServerInfo& out);

// The command that starts the server with this much memory.
std::vector<std::string> ServerCommand(const ServerInfo& info, int memoryMb);

// The server.properties values the launcher edits. Others in the file are kept.
struct ServerProperties {
	int port = 25565;
	int maxPlayers = 10;
	std::string motd = "A Minecraft Server";
	bool onlineMode = true;  // check players' Microsoft accounts
	bool pvp = true;
	std::string difficulty = "normal"; // peaceful, easy, normal, hard
	std::string gamemode = "survival"; // survival, creative, adventure, spectator
	int viewDistance = 10;
	bool whitelist = false;
};

ServerProperties ReadServerProperties(const std::filesystem::path& serverDir);
bool WriteServerProperties(const std::filesystem::path& serverDir, const ServerProperties& properties, std::string* err);

// eula.txt: the server won't start until eula=true. Only written when the player agrees.
bool EulaAccepted(const std::filesystem::path& serverDir);
bool AcceptEula(const std::filesystem::path& serverDir, std::string* err);

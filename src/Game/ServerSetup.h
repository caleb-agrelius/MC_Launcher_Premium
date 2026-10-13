#pragma once

#include "Game/GameFiles.h"
#include "Game/InstanceContent.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

class IHttpClient;

// One-click dedicated servers for installed packs: the pack's server-side files, its mod
// loader's server, and a Java to run it, in servers/<pack id>/. Modrinth packs are built
// from their modrinth.index.json; whole-pack installs (CurseForge, FTB, Technic,
// ATLauncher) from their installed game folder, with client-only mods turned off.

struct ServerSetupRequest {
	std::filesystem::path gameRoot;     // minecraft/ (version JSONs, Java runtimes, installers)
	std::filesystem::path extractedDir; // installs/<pack id>/ (the unzipped .mrpack)
	std::filesystem::path serverDir;    // servers/<pack id>/
	std::string javaPath;               // Settings > Minecraft's Java, or empty for Mojang's runtime
	// Whole-pack installs: given mod jars' SHA-1s, adds the client-only ones to clientOnly
	// (the launcher asks Modrinth). Optional. Returning false notes that the check failed.
	std::function<bool(const std::vector<std::string>& sha1s, std::set<std::string>& clientOnly)> findClientOnly;
	// The player's edits to the pack version (see ContentChanges): mod jars they added, copied
	// into the server's mods/ when it doesn't have them, and pack files they removed, which
	// the server doesn't download either.
	std::vector<std::filesystem::path> addedMods;
	std::set<std::string> removedFiles;
};

// What SetupServer installed, saved as serverDir/.mclp-server.json.
struct ServerInfo {
	std::string minecraftVersion;
	std::string loader;        // "Vanilla", "Fabric", "Quilt", "Forge", "NeoForge"
	std::string loaderVersion;
	std::string packVersion;
	std::string java;          // absolute path to java(.exe)
	std::vector<std::string> launchArgs; // after the JVM memory flags, e.g. { "-jar", "server.jar", "nogui" }
	std::vector<std::string> notes;      // what setup changed or couldn't check, for the player
	int javaMajor = 0;                   // the Java version Minecraft asks for; 0 if unknown (older setups)
};

// The argument file newer Forge and NeoForge installers write for this OS (win_args.txt on
// Windows, unix_args.txt elsewhere), which the server is started with as @file.
const char* ForgeArgsFileName();

// True for packs that can be turned into a server: an extracted .mrpack (modrinth.index.json)
// or a whole-pack install (pack.json and a minecraft/ folder).
bool CanCreateServer(const std::filesystem::path& extractedDir);

// Downloads and installs everything the server needs. Safe to run again to update the
// server after the pack was reinstalled; world, configs and server.properties are kept.
bool SetupServer(IHttpClient& http,
				 const ServerSetupRequest& request,
				 ServerInfo& out,
				 const ProgressFn& progress,
				 std::string* err,
				 const std::atomic<bool>* cancel);

// The files an old Forge server (Minecraft 1.6 to 1.12) loads from beside its universal jar: the
// Class-Path of the jar's META-INF/MANIFEST.MF, e.g. "libraries/net/minecraft/launchwrapper/1.12/
// launchwrapper-1.12.jar" and "minecraft_server.1.7.10.jar". Fails when the jar has none.
bool ReadForgeServerClassPath(const std::filesystem::path& forgeJar, std::vector<std::string>& out, std::string* err);

// Where an old Forge server's libraries are downloaded from, tried in order: the libraries
// Forge's retired files.minecraftforge.net used to serve are now on these.
const std::vector<std::string>& LegacyForgeLibraryMirrors();

// Reads .mclp-server.json. False if the server hasn't been set up.
bool LoadServerInfo(const std::filesystem::path& serverDir, ServerInfo& out);

// Deletes serverDir and everything in it (world, configs, logs). The server must be stopped.
bool RemoveServer(const std::filesystem::path& serverDir, std::string* err);

// True when serverDir holds a server that SetupServer finished (.mclp-server.json).
bool HasServerFiles(const std::filesystem::path& serverDir);

// A mod in serverDir/mods/ (see InstanceContent.h). Disabled mods are renamed
// "<name>.disabled"; SetupServer doesn't download or copy a file again while its .disabled copy exists.
using ServerMod = ContentFile;

// The server's mods (.jar and .jar.disabled files), sorted by name ignoring case.
std::vector<ServerMod> ListServerMods(const std::filesystem::path& serverDir);

// Renames mods/<fileName> to or from "<fileName>.disabled". The server must be stopped.
bool SetServerModEnabled(const std::filesystem::path& serverDir, const std::string& fileName, bool enabled,
						 std::string* err);

// The command that starts the server with this much memory.
std::vector<std::string> ServerCommand(const ServerInfo& info, int memoryMb);

// The server.properties values the launcher edits. Others in the file are kept.
struct ServerProperties {
	int port = 25565;
	std::string serverIp;    // the address the server listens on; empty = every network adapter
	int maxPlayers = 10;
	std::string motd = "A Minecraft Server";
	bool onlineMode = true;  // check players' Microsoft accounts
	bool pvp = true;
	std::string difficulty = "normal"; // peaceful, easy, normal, hard
	std::string gamemode = "survival"; // survival, creative, adventure, spectator
	int viewDistance = 10;
	bool whitelist = false;
};

// True for an empty host IP (listen everywhere), a plain IPv4 address like "192.168.1.20",
// or "localhost" (Minecraft resolves it to this PC's loopback address).
bool IsValidServerIp(const std::string& address);
// True for an address only this PC can reach: "localhost" or 127.x.x.x.
bool IsLoopbackServerIp(const std::string& address);

ServerProperties ReadServerProperties(const std::filesystem::path& serverDir);
bool WriteServerProperties(const std::filesystem::path& serverDir, const ServerProperties& properties, std::string* err);

// docker-minecraft-server (https://github.com/itzg/docker-minecraft-server): a compose file
// that runs the prepared server folder in the itzg/minecraft-server image, with the same
// loader, versions, memory and port. server.properties is left to the launcher
// (SKIP_SERVER_PROPERTIES), and EULA is TRUE only when the player accepted it.

// The image tag for a Java version: java8, java11, java17, java21 or java25. Without a
// known Java version (0), it is worked out from the Minecraft version.
std::string DockerImageTag(int javaMajor, const std::string& minecraftVersion);

// The docker-compose.yml text. packId names the container ("mclp-<pack id>").
std::string DockerComposeText(const std::string& packId, const ServerInfo& info, const ServerProperties& properties,
							  int memoryMb, bool eulaAccepted);

// Writes serverDir/docker-compose.yml.
bool WriteDockerCompose(const std::filesystem::path& serverDir, const std::string& packId, const ServerInfo& info,
						int memoryMb, std::string* err);

// eula.txt: the server won't start until eula=true. Only written when the player agrees.
bool EulaAccepted(const std::filesystem::path& serverDir);
bool AcceptEula(const std::filesystem::path& serverDir, std::string* err);

// Whether the server runs in Docker (docker-minecraft-server) instead of directly with Java.
// Kept as the marker file .mclp-docker in the server folder.
bool ServerUsesDocker(const std::filesystem::path& serverDir);
bool SetServerUsesDocker(const std::filesystem::path& serverDir, bool docker, std::string* err);

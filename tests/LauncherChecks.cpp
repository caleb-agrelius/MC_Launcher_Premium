#include "App/FramePacing.h"
#include "Game/InstanceContent.h"
#include "Game/JarMods.h"
#include "Game/JavaFinder.h"
#include "Game/Loaders.h"
#include "Game/PackCreator.h"
#include "Game/PackInstance.h"
#include "Game/ServerSetup.h"
#include "Network/CachingHttpClient.h"
#include "Network/HttpClient.h"
#include "Network/Modrinth.h"
#include "Network/OfflineHttpClient.h"
#include "Network/TracingHttpClient.h"
#include "Platform/Platform.h"
#include "Storage/Library.h"
#include "Storage/Settings.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/DebugTrace.h"
#include "Util/Paths.h"
#include "Util/Process.h"
#include "Util/Sha1.h"
#include "Util/SharedFiles.h"
#include "Util/Text.h"
#include "Util/TracedFs.h"
#include "imgui_internal.h"

#include <miniz/miniz.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using json = nlohmann::json;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
		throw std::runtime_error(message);
}

static void Write(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out << text;
	Check(out.good(), "Could not write fixture");
}

static std::string Read(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

static void SettingsChecks(const fs::path& dir)
{
	const fs::path path = dir / "settings.json";
	Settings settings;
	settings.window = { true, -1400, 120, 1280, 760, true };
	settings.discoverSources = { "modrinth", "ftb" };
	settings.paneWidths = { { "discover", 412.0f }, { "servers", 5.0f } };
	Check(SaveSettings(path.string(), settings), "Settings save failed");
	const Settings restored = LoadSettings(path.string());
	Check(restored.window.saved && restored.window.x == -1400 && restored.window.y == 120 &&
		restored.window.width == 1280 && restored.window.height == 760 && restored.window.maximized,
		"Window placement did not round-trip");
	Check(restored.discoverSources == settings.discoverSources, "Discover filters did not round-trip");
	Check(restored.animationSpeed == 1.0f, "The animation speed default changed");
	settings.animationSpeed = 1.5f;
	Check(SaveSettings(path.string(), settings) && LoadSettings(path.string()).animationSpeed == 1.5f, "Animation speed did not round-trip");
	settings.animationSpeed = 99.0f;
	Check(SaveSettings(path.string(), settings) && LoadSettings(path.string()).animationSpeed == kMaxAnimationSpeed, "Animation speed was not clamped");
	Check(restored.maxFps == 0 && restored.lowerIdleFrameRate, "The frame rate defaults changed");
	settings.maxFps = 120;
	settings.lowerIdleFrameRate = false;
	{
		const Settings fps = (SaveSettings(path.string(), settings), LoadSettings(path.string()));
		Check(fps.maxFps == 120 && !fps.lowerIdleFrameRate, "Frame rate settings did not round-trip");
	}
	settings.maxFps = 5;
	Check(SaveSettings(path.string(), settings) && LoadSettings(path.string()).maxFps == kMinMaxFps, "A tiny frame rate cap was not raised to the minimum");
	settings.maxFps = 5000;
	Check(SaveSettings(path.string(), settings) && LoadSettings(path.string()).maxFps == kMaxMaxFps, "A huge frame rate cap was not clamped");
	settings.maxFps = 0;
	settings.lowerIdleFrameRate = true;
	Check(SaveSettings(path.string(), settings) && LoadSettings(path.string()).maxFps == 0, "Match display (0) must stay 0, not be raised to the minimum");
	Check(restored.discoverGridScale == 100, "The grid scale default changed");
	settings.discoverGridScale = 130;
	Check(SaveSettings(path.string(), settings) && LoadSettings(path.string()).discoverGridScale == 130, "Grid scale did not round-trip");
	settings.discoverGridScale = 900;
	Check(SaveSettings(path.string(), settings) && LoadSettings(path.string()).discoverGridScale == kMaxGridScale, "Grid scale was not clamped");
	settings.discoverGridScale = 100;
	settings.animationSpeed = 1.0f;
	Check(SaveSettings(path.string(), settings), "Settings save failed");
	Check(restored.paneWidths.size() == 2 && restored.paneWidths.at("discover") == 412.0f &&
		restored.paneWidths.at("servers") == kMinPaneWidth, "Pane widths did not round-trip within limits");
	Write(path, R"({"window":{"width":0,"height":760}})");
	Check(!LoadSettings(path.string()).window.saved, "Invalid placement was accepted");
	Check(!LoadSettings((dir / "missing.json").string()).window.saved, "Missing settings changed defaults");
}

static void ServerChecks(const fs::path& dir, const fs::path& executable)
{
	HttpClient http;
	ServerSetupRequest request;
	request.extractedDir = dir / "pack";
	request.serverDir = dir / "server";
	request.gameRoot = dir / "game";
	request.javaPath = executable.string();
	json index = {
		{ "versionId", "v1" }, { "dependencies", { { "minecraft", "1.20.1" } } },
		{ "files", json::array({
			{ { "path", "mods/shared.jar" }, { "downloads", { "unused" } }, { "fileSize", 4 } },
			{ { "path", "mods/client.jar" }, { "env", { { "server", "unsupported" } } }, { "downloads", { "unused" } } },
			{ { "path", "mods/server.jar" }, { "env", { { "client", "unsupported" } } }, { "downloads", { "unused" } }, { "fileSize", 4 } }
		}) }
	};
	Write(request.extractedDir / "modrinth.index.json", index.dump());
	MrpackIndex client, server;
	std::string error;
	Check(ReadMrpackIndex(request.extractedDir, client, &error), "Client index failed");
	Check(ReadMrpackIndex(request.extractedDir, server, &error, true), "Server index failed");
	Check(client.files.size() == 2 && client.files[1].path == "mods/client.jar", "Client environment filter failed");
	Check(server.files.size() == 2 && server.files[1].path == "mods/server.jar", "Server environment filter failed");
	Write(request.serverDir / "mods/shared.jar", "stub");
	Write(request.serverDir / "mods/server.jar", "stub");
	Write(request.serverDir / "server.jar", "stub");
	Write(request.gameRoot / "versions/1.20.1/1.20.1.json",
		R"({"id":"1.20.1","mainClass":"fixture.Main","downloads":{"server":{"url":"unused","size":4}}})");
	Write(request.extractedDir / "overrides/config/shared.txt", "common");
	Write(request.extractedDir / "server-overrides/config/shared.txt", "server");
	Write(request.extractedDir / "client-overrides/client.txt", "client only");
	// An override nested past 260 characters, as some packs' default configs are.
	fs::path deepOverride = "config";
	while ((request.serverDir / deepOverride).native().size() < 280)
		deepOverride /= "modpack_defaults";
	Write(LongPath(fs::absolute(request.extractedDir / "overrides" / deepOverride / "deep.cfg")), "deep");

	// Playing copies the same overrides into the client's game folder.
	const fs::path instanceDir = dir / "instance";
	Write(instanceDir / "mods/shared.jar", "stub");
	Write(instanceDir / "mods/client.jar.disabled", "stub"); // turned off: not downloaded again
	Check(PrepareInstance(http, request.extractedDir, instanceDir, client, {}, &error, nullptr), error);
	Check(!fs::exists(instanceDir / "mods/client.jar"), "A mod the player turned off was downloaded again");
	Check(Read(LongPath(fs::absolute(instanceDir / deepOverride / "deep.cfg"))) == "deep" &&
		Read(instanceDir / "client.txt") == "client only", "Deep client override was not copied");
	ServerInfo info;
	Write(instanceDir / "mods/added.jar", "mine");
	request.addedMods = { instanceDir / "mods/added.jar" };
	fs::remove(request.serverDir / "mods/server.jar");
	request.removedFiles = { "mods/server.jar" }; // its URL is unusable, so downloading it would fail
	Check(SetupServer(http, request, info, {}, &error, nullptr), error);
	Check(Read(request.serverDir / "mods/added.jar") == "mine" && !fs::exists(request.serverDir / "mods/server.jar") &&
		!info.notes.empty() && info.notes.back().find("added.jar") != std::string::npos,
		"The player's added and removed mods didn't reach the server");
	fs::remove(request.serverDir / "mods/added.jar");
	Write(request.serverDir / "mods/server.jar", "stub");
	request.addedMods.clear();
	request.removedFiles.clear();
	Check(Read(LongPath(fs::absolute(request.serverDir / deepOverride / "deep.cfg"))) == "deep", "Deep override was not copied");
	Check(Read(request.serverDir / "config/shared.txt") == "server", "Server override did not win");
	Check(!fs::exists(request.serverDir / "client.txt") && !fs::exists(request.serverDir / "mods/client.jar"),
		"Client files leaked into server");
	Check(LoadServerInfo(request.serverDir, info) && info.launchArgs == std::vector<std::string>({ "-jar", "server.jar", "nogui" }),
		"Server launch info was not saved");
	Check(ServerCommand(info, 6144)[1] == "-Xmx6144M", "Server memory was not applied");
	Check(!EulaAccepted(request.serverDir), "EULA was accepted without consent");
	Check(!ServerUsesDocker(request.serverDir) && SetServerUsesDocker(request.serverDir, true, &error) &&
		ServerUsesDocker(request.serverDir) && SetServerUsesDocker(request.serverDir, false, &error) &&
		!ServerUsesDocker(request.serverDir), "The Docker switch didn't round-trip");
	Check(AcceptEula(request.serverDir, &error) && EulaAccepted(request.serverDir), "EULA acceptance failed");
	Write(request.serverDir / "server.properties", "# keep me\ncustom-setting=yes\nserver-port=25565\n");
	ServerProperties props;
	props.port = 25570;
	props.onlineMode = false;
	props.serverIp = "100.64.1.2";
	Check(WriteServerProperties(request.serverDir, props, &error), "Properties write failed");
	Check(ReadServerProperties(request.serverDir).port == 25570 && !ReadServerProperties(request.serverDir).onlineMode &&
		ReadServerProperties(request.serverDir).serverIp == "100.64.1.2", "Properties did not round-trip");
	Check(IsValidServerIp("localhost") && IsValidServerIp("LocalHost") && IsLoopbackServerIp("localhost") &&
		IsLoopbackServerIp("127.0.0.1") && !IsLoopbackServerIp("192.168.1.20") && !IsLoopbackServerIp(""),
		"localhost wasn't accepted as a loopback host IP");
	Check(IsValidServerIp("") && IsValidServerIp("192.168.1.20") && IsValidServerIp("10.0.0.255"),
		"A valid host IP was rejected");
	Check(!IsValidServerIp("256.1.1.1") && !IsValidServerIp("192.168.1") && !IsValidServerIp("my-pc") &&
		!IsValidServerIp("1.2.3.4 ") && !IsValidServerIp("01.2.3.4"), "An invalid host IP was accepted");
	for (const Platform::NetworkAddress& address : Platform::LocalNetworkAddresses())
		Check(IsValidServerIp(address.address) && address.address.rfind("127.", 0) != 0 &&
			address.address.rfind("169.254.", 0) != 0, "Unusable adapter address listed: " + address.address);
	Check(Read(request.serverDir / "server.properties").find("custom-setting=yes") != std::string::npos,
		"Unknown server property was lost");
	// Turning mods off: the update below must neither download server.jar (its URL is unusable)
	// nor copy extra.jar from the overrides.
	std::vector<ServerMod> mods = ListServerMods(request.serverDir);
	Check(mods.size() == 2 && mods[0].fileName == "server.jar" && mods[1].fileName == "shared.jar" && mods[0].enabled,
		"Server mods were not listed");
	Check(SetServerModEnabled(request.serverDir, "server.jar", false, &error), error);
	Check(fs::exists(request.serverDir / "mods/server.jar.disabled") && !fs::exists(request.serverDir / "mods/server.jar"),
		"Mod was not disabled");
	Write(request.extractedDir / "overrides/mods/Extra.jar", "override mod");
	Write(request.serverDir / "mods/Extra.jar.disabled", "override mod");
	Check(!SetServerModEnabled(request.serverDir, "../server.jar", true, &error), "Unsafe mod name was accepted");
	Write(request.serverDir / "config/shared.txt", "user config");
	Write(request.serverDir / "world/level.dat", "world");
	index["versionId"] = "v2";
	Write(request.extractedDir / "modrinth.index.json", index.dump());
	Check(SetupServer(http, request, info, {}, &error, nullptr), error);
	Check(Read(request.serverDir / "config/shared.txt") == "user config" &&
		Read(request.serverDir / "world/level.dat") == "world" && EulaAccepted(request.serverDir) &&
		ReadServerProperties(request.serverDir).port == 25570, "Server update overwrote player data");
	Check(!fs::exists(request.serverDir / "mods/server.jar") && !fs::exists(request.serverDir / "mods/Extra.jar"),
		"Update brought back a disabled mod");
	mods = ListServerMods(request.serverDir);
	Check(mods.size() == 3 && mods[0].fileName == "Extra.jar" && !mods[0].enabled && !mods[1].enabled && mods[2].enabled,
		"Disabled mods were not listed");
	Check(SetServerModEnabled(request.serverDir, "server.jar", true, &error) &&
		fs::exists(request.serverDir / "mods/server.jar") && !fs::exists(request.serverDir / "mods/server.jar.disabled"),
		"Mod was not enabled again");
	std::atomic<bool> cancel{ true };
	Check(!SetupServer(http, request, info, {}, &error, &cancel) && error == "Cancelled", "Setup ignored cancellation");
#ifdef _WIN32
	// This executable acts as the installer; cached fixtures keep the checks offline.
	index["dependencies"]["forge"] = "47.2.0";
	Write(request.extractedDir / "modrinth.index.json", index.dump());
	Write(request.gameRoot / "installers/forge-1.20.1-47.2.0-installer.jar", "stub");
	Write(request.serverDir / "libraries/net/minecraftforge/forge/1.20.1-47.1.0/win_args.txt", "stale");
	Write(request.serverDir / "libraries/net/minecraftforge/forge/1.20.1-47.2.0/win_args.txt", "current");
	Check(SetupServer(http, request, info, {}, &error, nullptr), error);
	Check(info.launchArgs[0] == "@libraries/net/minecraftforge/forge/1.20.1-47.2.0/win_args.txt",
		"Forge update selected stale loader arguments");
#endif
	request.serverDir = dir / "blocked";
	fs::create_directories(request.serverDir / ".mclp-server.json");
	Write(request.serverDir / "mods/shared.jar", "stub");
	Write(request.serverDir / "mods/server.jar", "stub");
	Write(request.serverDir / "server.jar", "stub");
	index["dependencies"].erase("forge");
	Write(request.extractedDir / "modrinth.index.json", index.dump());
	Check(!SetupServer(http, request, info, {}, &error, nullptr), "Setup reported success when metadata could not be saved");
	index["files"][0]["path"] = "../escape.jar";
	Write(request.extractedDir / "modrinth.index.json", index.dump());
	Check(!ReadMrpackIndex(request.extractedDir, server, &error, true), "Unsafe server path was accepted");
}

// A whole-pack install (CurseForge, FTB, ...) hosted from its installed game folder.
static void WholePackServerChecks(const fs::path& dir, const fs::path& executable)
{
	HttpClient http;
	ServerSetupRequest request;
	request.extractedDir = dir / "whole-pack";
	request.serverDir = dir / "whole-server";
	request.gameRoot = dir / "game"; // the 1.20.1 version file from ServerChecks
	request.javaPath = executable.string();
	Write(request.extractedDir / "pack.json",
		R"({"source":"ftb","version":"1.0","minecraft":"1.20.1","loader":"","clientOnlyFiles":["mods/Flagged.jar"]})");
	const fs::path game = request.extractedDir / "minecraft";
	Write(game / "mods/common.jar", "common");
	Write(game / "mods/Flagged.jar", "flagged by the source");
	Write(game / "mods/lookedup.jar", "client only on modrinth");
	Write(game / "config/pack.cfg", "pack config");
	Write(game / "saves/world/level.dat", "client world");
	Write(game / "options.txt", "client options");
	Write(request.serverDir / "server.jar", "stub");
	Check(CanCreateServer(request.extractedDir), "Whole-pack install was not hostable");
	const std::string lookedUpHash = Sha1File((game / "mods/lookedup.jar").string());
	std::vector<std::string> asked;
	request.findClientOnly = [&](const std::vector<std::string>& sha1s, std::set<std::string>& clientOnly) {
		asked = sha1s;
		clientOnly.insert(lookedUpHash);
		return true;
	};
	ServerInfo info;
	std::string error;
	Check(SetupServer(http, request, info, {}, &error, nullptr), error);
	Check(asked.size() == 2, "Client-only lookup asked about the wrong mods");
	Check(Read(request.serverDir / "mods/common.jar") == "common" && Read(request.serverDir / "config/pack.cfg") == "pack config",
		"Whole-pack files were not copied");
	Check(fs::exists(request.serverDir / "mods/Flagged.jar.disabled") && !fs::exists(request.serverDir / "mods/Flagged.jar") &&
		fs::exists(request.serverDir / "mods/lookedup.jar.disabled") && !fs::exists(request.serverDir / "mods/lookedup.jar"),
		"Client-only mods were not turned off");
	Check(!fs::exists(request.serverDir / "saves") && !fs::exists(request.serverDir / "options.txt"),
		"Client-only files were copied to the server");
	Check(info.notes.size() == 1 && info.notes[0].find("2 client-only mod(s)") != std::string::npos, "Turned-off mods were not noted");
	Check(LoadServerInfo(request.serverDir, info) && info.notes.size() == 1 && info.minecraftVersion == "1.20.1",
		"Whole-pack server info was not saved");

	// An update keeps the server's config and a mod the player turned back on.
	Write(request.serverDir / "config/pack.cfg", "server config");
	Check(SetServerModEnabled(request.serverDir, "lookedup.jar", true, &error), error);
	Write(request.extractedDir / "pack.json",
		R"({"source":"ftb","version":"1.1","minecraft":"1.20.1","loader":"","clientOnlyFiles":["mods/Flagged.jar"]})");
	request.findClientOnly = [](const std::vector<std::string>&, std::set<std::string>&) { return false; };
	Check(SetupServer(http, request, info, {}, &error, nullptr), error);
	Check(Read(request.serverDir / "config/pack.cfg") == "server config" && fs::exists(request.serverDir / "mods/lookedup.jar") &&
		!fs::exists(request.serverDir / "mods/Flagged.jar"), "Whole-pack update overwrote the player's choices");
	Check(info.notes.size() == 1 && info.notes[0].find("Modrinth couldn't be reached") != std::string::npos,
		"A failed client-only check was not noted");
}

static void DockerChecks(const fs::path& dir)
{
	Check(DockerImageTag(0, "1.12.2") == "java8" && DockerImageTag(0, "1.18.2") == "java17" &&
		DockerImageTag(0, "1.20.4") == "java17" && DockerImageTag(0, "1.20.5") == "java21" &&
		DockerImageTag(0, "1.21.1") == "java21" && DockerImageTag(0, "26.1.2") == "java25" &&
		DockerImageTag(17, "1.21.1") == "java17" && DockerImageTag(25, "") == "java25", "Wrong Docker image tag");
	ServerInfo info;
	info.minecraftVersion = "1.20.1";
	info.loader = "Fabric";
	info.loaderVersion = "0.15.11";
	info.javaMajor = 17;
	ServerProperties props;
	props.port = 25570;
	const std::string text = DockerComposeText("My_Pack.v2", info, props, 6144, true);
	for (const char* expected : { "image: itzg/minecraft-server:java17", "container_name: mclp-my_pack.v2", "\"25570:25570\"",
			"EULA: \"TRUE\"", "TYPE: \"FABRIC\"", "VERSION: \"1.20.1\"", "FABRIC_LOADER_VERSION: \"0.15.11\"", "MEMORY: \"6144M\"",
			"SKIP_SERVER_PROPERTIES: \"true\"", "- ./:/data" })
		Check(text.find(expected) != std::string::npos, std::string("Compose file is missing ") + expected);
	info.loader = "Vanilla";
	Check(DockerComposeText("p", info, props, 2048, false).find("EULA: \"FALSE\"") != std::string::npos &&
		DockerComposeText("p", info, props, 2048, false).find("LOADER_VERSION") == std::string::npos, "Vanilla compose file is wrong");

	const fs::path server = dir / "docker-server";
	props.serverIp = "192.168.1.20";
	std::string error;
	Check(WriteServerProperties(server, props, &error), error);
	Check(!WriteDockerCompose(server, "p", info, 2048, &error) && !fs::exists(server / "docker-compose.yml"),
		"Compose file was written for a server bound to one address");
	props.serverIp.clear();
	Check(WriteServerProperties(server, props, &error) && AcceptEula(server, &error), error);
	Check(WriteDockerCompose(server, "p", info, 2048, &error) &&
		Read(server / "docker-compose.yml").find("EULA: \"TRUE\"") != std::string::npos, "Compose file was not written");
}

static void TextChecks()
{
	const std::string markdown =
		"[![Install](https://i.ibb.co/x.png)](https://download.fo/install)\n"
		"# Fabulously **Optimized**\n\n\n"
		"A [simple](https://example.com) pack with `code` and ~~old~~ *new* text.\n"
		"- first item\n"
		"* second item\n"
		"---\n"
		"| Mod | Side |\n|---|---|\n| Sodium | client |\n"
		"> quoted\n";
	const std::string plain = MarkupToPlainText(markdown);
	Check(plain == "Fabulously Optimized\n\nA simple pack with code and old new text.\n\xC2\xB7 first item\n\xC2\xB7 second item\n\n"
		"Mod   Side\nSodium   client\nquoted", "Markdown was not converted: [" + plain + "]");
	const std::string html = MarkupToPlainText(
		"<p>Fish &amp; chips&nbsp;&#8217;n&#x21;</p><script>alert(1)</script><ul><li>One</li><li>Two</li></ul>"
		"<img src=\"x.png\"><h2>Next</h2>Line<br>break &hellip; &mdash; done");
	Check(html == "Fish & chips 'n!\n\n\xC2\xB7 One\n\xC2\xB7 Two\n\nNext\n\nLine\nbreak ... - done",
		"HTML was not converted: [" + html + "]");
	Check(ShortenText("short", 10) == "short" && ShortenText("one two three four", 10) == "one two..." &&
		ShortenText("line\nbreak", 20) == "line break", "Text was not shortened");
}

static void ServerRemoveChecks(const fs::path& dir)
{
	const fs::path server = dir / "removable";
	// Deeper than 260 characters, like a modded server's config folders can be.
	fs::path deep = server / "world";
	while (deep.native().size() < 300)
		deep /= "nested-folder-name";
	Write(LongPath(deep / "file.txt"), "deep");
	Write(server / ".mclp-server.json", "{}");
	Check(HasServerFiles(server), "Server files were not detected");
	std::string error;
	Check(RemoveServer(server, &error), error);
	Check(!fs::exists(server) && !HasServerFiles(server), "Server folder was not deleted");
	Check(!RemoveServer(fs::path(server.root_path()), &error), "A drive root was accepted as a server folder");
}

// Updating a pack copies the player's worlds and options into the new version's game
// folder, but not the old version's mods or configs, and never over the new version's files.
static void PlayerDataChecks(const fs::path& dir)
{
	const fs::path from = dir / "update-from";
	const fs::path to = dir / "update-to";
	fs::remove_all(ExtendedPath(from));
	fs::remove_all(ExtendedPath(to));
	Write(from / "saves" / "World" / "level.dat", "world");
	fs::path deep = from / "saves" / "World" / "data";
	while (deep.native().size() < 300)
		deep /= "nested-folder-name";
	Write(LongPath(deep / "map.dat"), "deep");
	Write(from / "screenshots" / "shot.png", "png");
	Write(from / "servers.dat", "servers");
	Write(from / "options.txt", "player options");
	Write(from / "mods" / "old.jar", "old mod");
	Write(from / "config" / "old.toml", "old config");
	Write(to / "options.txt", "pack options");
	Write(to / "mods" / "new.jar", "new mod");

	std::size_t copied = 0;
	std::string error;
	Check(CarryOverPlayerData(from, to, copied, &error), error);
	Check(copied == 4, "Expected 4 copied files, got " + std::to_string(copied));
	Check(Read(to / "saves" / "World" / "level.dat") == "world", "World was not copied");
	const fs::path deepCopy = to / deep.lexically_relative(from) / "map.dat";
	Check(fs::exists(LongPath(deepCopy)), "Deeply nested world file was not copied");
	Check(Read(to / "screenshots" / "shot.png") == "png" && Read(to / "servers.dat") == "servers",
		"Screenshots or server list were not copied");
	Check(Read(to / "options.txt") == "pack options", "The new version's options were overwritten");
	Check(!fs::exists(to / "mods" / "old.jar") && !fs::exists(to / "config"), "Old mods or configs were copied");
	Check(CarryOverPlayerData(from, to, copied, &error) && copied == 0, "A second run copied files again");
	Check(CarryOverPlayerData(dir / "missing", to, copied, &error) && copied == 0, "A missing old folder failed");
	fs::remove_all(ExtendedPath(from));
	fs::remove_all(ExtendedPath(to));
}

// Every color ImGui draws with comes from the theme: none may keep ImGui's stock value
// (e.g. the default blue of dialog title bars and tabs). Needs an ImGui context.
static void ThemeColorChecks()
{
	for (std::size_t theme = 0; theme < Theme::BuiltInThemeCount(); ++theme) {
		Theme::Apply(theme, 1.0f, 1.0f);
		ImGuiStyle stock;
		if (Theme::C().light)
			ImGui::StyleColorsLight(&stock);
		else
			ImGui::StyleColorsDark(&stock);
		const ImGuiStyle& style = ImGui::GetStyle();
		for (int i = 0; i < ImGuiCol_COUNT; ++i) {
			// Fully transparent in both: nothing is drawn, so there's nothing to theme.
			if (style.Colors[i].w == 0.0f && stock.Colors[i].w == 0.0f)
				continue;
			const ImVec4& a = style.Colors[i];
			const ImVec4& b = stock.Colors[i];
			Check(a.x != b.x || a.y != b.y || a.z != b.z || a.w != b.w,
				std::string("Theme ") + Theme::Themes()[theme].id + " leaves " + ImGui::GetStyleColorName(i) + " at ImGui's default");
		}
	}
	Theme::Apply(0, 1.0f, 1.0f);
}

static void CardChecks()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(800, 600);
	io.DeltaTime = 1.0f / 60.0f;
	io.Fonts->AddFontDefault();
	io.Fonts->Build();
	io.Fonts->SetTexID(ImTextureID(1));
	ThemeColorChecks();
	Widgets::SetReduceMotion(true);
	float measured = 0.0f;
	for (int frame = 0; frame < 6; ++frame) {
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(600, 400));
		ImGui::Begin("checks", nullptr, ImGuiWindowFlags_NoSavedSettings);
		const ImGuiID key = ImGui::GetID("card");
		if (frame == 3)
			ImGui::Dummy(ImVec2(1, 800)); // hide the card below the viewport
		Widgets::BeginEntrance(0.0);
		Widgets::BeginCard("card");
		ImGui::Dummy(ImVec2(100, 120));
		Widgets::EndCard();
		Widgets::EndEntrance();
		const float height = ImGui::GetStateStorage()->GetFloat(key);
		if (frame == 2) {
			measured = height;
			Check(height >= 120, "Card did not grow to fit content");
		}
		if (frame >= 3)
			Check(std::fabs(height - measured) <= 1.0f, "Card height changed when hidden or revisited");
		ImGui::End();
		ImGui::Render();
	}
	ImGui::DestroyContext();
}

static void ProcessChecks(const fs::path& dir, const fs::path& executable)
{
#ifdef _WIN32
	ServerProcess process;
	std::string error;
	Check(process.Start({ executable.string(), "--console-fixture", "argument with spaces" }, dir,
		dir / "console.log", &error), error);
	Check(!process.Start({ executable.string() }, dir, dir / "console.log", &error), "Process allowed a second start");
	process.Send("say hello");
	process.Send("stop");
	Check(process.WaitForExit(5000) && process.ExitCode() == 0, "Console process did not stop cleanly");
	for (int attempt = 0; attempt < 50; ++attempt) {
		if (Read(dir / "console.log").find("received:stop") != std::string::npos)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	const std::string log = Read(dir / "console.log");
	Check(log.find("argument with spaces") != std::string::npos && log.find("received:say hello") != std::string::npos &&
		log.find("received:stop") != std::string::npos, "Console input, output or argument quoting failed");
	Check(!process.Lines().empty(), "Console did not collect lines");

	// Cancelling a running program returns promptly and, with killOnCancel, ends it.
	std::atomic<bool> cancel{ false };
	std::thread canceller([&cancel]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		cancel.store(true);
	});
	const auto started = std::chrono::steady_clock::now();
	int exitCode = 0;
	const bool ran = RunProcess({ executable.string(), "--sleep-fixture", "20" }, dir, dir / "sleep.log", true, &exitCode,
		&error, &cancel, true);
	canceller.join();
	Check(!ran && error == "Cancelled" && std::chrono::steady_clock::now() - started < std::chrono::seconds(3),
		"Cancelling a running program did not return promptly");

	// A program the server starts must not get the server's console pipe, or the console
	// would never see the server exit while that program runs.
	{
		const auto stopping = std::chrono::steady_clock::now();
		{
			ServerProcess server;
			Check(server.Start({ executable.string(), "--console-fixture", "parent" }, dir, dir / "parent.log", &error), error);
			server.Send("spawn");
			server.Send("stop");
			Check(server.WaitForExit(5000), "Console process with a child did not stop");
			for (int attempt = 0; attempt < 200; ++attempt) {
				const std::vector<std::string> lines = server.Lines();
				if (!lines.empty() && lines.back() == "received:stop")
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}
		Check(std::chrono::steady_clock::now() - stopping < std::chrono::seconds(4),
			"Console reader waited on a child that inherited its pipe");
	}
#endif
}

// A jar holding the given entries, written with miniz.
static void WriteJar(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& entries)
{
	fs::create_directories(path.parent_path());
	mz_zip_archive zip{};
	Check(mz_zip_writer_init_heap(&zip, 0, 0) != 0, "Could not start a jar");
	for (const auto& [name, text] : entries)
		Check(mz_zip_writer_add_mem(&zip, name.c_str(), text.data(), text.size(), MZ_DEFAULT_COMPRESSION) != 0, "Could not add " + name);
	void* data = nullptr;
	std::size_t size = 0;
	Check(mz_zip_writer_finalize_heap_archive(&zip, &data, &size) != 0, "Could not finish a jar");
	Write(path, std::string(static_cast<const char*>(data), size));
	mz_zip_writer_end(&zip);
}

// Shared mod files: keys, shareable paths, sharing existing copies, linking new installs,
// refusing a damaged stored copy, and pruning unused files.
static void SharedFileChecks(const fs::path& dir)
{
	Check(SharedFiles::KeyFor("ABCDEF0123456789ABCDEF0123456789ABCDEF01") == "sha1-abcdef0123456789abcdef0123456789abcdef01" &&
		SharedFiles::KeyFor("", "0123456789abcdef0123456789abcdef") == "md5-0123456789abcdef0123456789abcdef" &&
		SharedFiles::KeyFor("short", "").empty(), "Share keys");
	Check(SharedFiles::IsShareablePath("mods/a.jar") && SharedFiles::IsShareablePath("resourcepacks/x.zip") &&
		!SharedFiles::IsShareablePath("config/a.json") && !SharedFiles::IsShareablePath("saves/w/level.dat") &&
		!SharedFiles::IsShareablePath("../mods/a.jar"), "Only mods and resource/shader packs are shared");

	const fs::path store = dir / "shared" / "store";
	const fs::path a = dir / "shared" / "packA", b = dir / "shared" / "packB", c = dir / "shared" / "packC";
	const std::string jar(200000, 'j');
	Write(a / "mods" / "same.jar", jar);
	Write(b / "mods" / "same.jar", jar);
	Write(b / "mods" / "other.jar", "different");
	Write(b / "config" / "same.jar", jar); // not in a shared folder
	SharedFiles::Configure(store, true);
	const SharedFiles::ShareResult shared = SharedFiles::ShareExisting({ a, b }, nullptr, nullptr);
	Check(shared.error.empty(), "Sharing failed: " + shared.error);
	Check(shared.scanned == 3 && shared.linked == 1 && shared.saved == jar.size(),
		"One duplicate should be shared (linked " + std::to_string(shared.linked) + ")");
	Check(fs::equivalent(a / "mods" / "same.jar", b / "mods" / "same.jar") && Read(b / "mods" / "same.jar") == jar,
		"The copies should now be one file with the same contents");
	Check(!fs::equivalent(b / "config" / "same.jar", a / "mods" / "same.jar"), "Configs are never shared");
	SharedFiles::Stats stats = SharedFiles::StoreStats();
	Check(stats.files == 2 && stats.shared == 1 && stats.saved == jar.size(), "Store stats");
	Check(SharedFiles::ShareExisting({ a, b }, nullptr, nullptr).linked == 0, "Sharing again changes nothing");

	// A new install of the same file links to the stored copy.
	const std::string key = SharedFiles::KeyFor(Sha1File((a / "mods" / "same.jar").string()));
	Check(SharedFiles::LinkTo(key, jar.size(), c / "mods" / "same.jar") &&
		fs::equivalent(c / "mods" / "same.jar", a / "mods" / "same.jar"), "A new install should link to the stored copy");
	Check(!SharedFiles::LinkTo(key, jar.size() + 1, c / "mods" / "wrong-size.jar"), "A size mismatch isn't linked");

	// Turning a mod off in one pack (a rename) leaves the others alone.
	fs::rename(c / "mods" / "same.jar", c / "mods" / "same.jar.disabled");
	Check(fs::exists(a / "mods" / "same.jar") && fs::exists(b / "mods" / "same.jar"), "Disabling in one pack keeps the others");

	// A stored copy whose contents changed is dropped instead of handed out.
	const std::string otherKey = SharedFiles::KeyFor(Sha1File((b / "mods" / "other.jar").string()));
	{
		std::ofstream damage(b / "mods" / "other.jar", std::ios::binary | std::ios::in | std::ios::out);
		damage << "X"; // writes through the link into the stored copy
	}
	Check(!SharedFiles::LinkTo(otherKey, 0, c / "mods" / "other.jar") && !fs::exists(c / "mods" / "other.jar"),
		"A damaged stored copy must not be linked");

	// Off: nothing is linked or stored.
	SharedFiles::Configure(store, false);
	Check(!SharedFiles::LinkTo(key, jar.size(), c / "mods" / "again.jar"), "Sharing off links nothing");
	SharedFiles::Configure(store, true);

	// Prune deletes stored files no pack uses any more.
	fs::remove_all(a);
	fs::remove_all(b);
	fs::remove_all(c);
	const std::uint64_t freed = SharedFiles::Prune();
	const SharedFiles::Stats left = SharedFiles::StoreStats();
	Check(freed == jar.size() && left.files == 0, "Prune should free unused stored files (freed " + std::to_string(freed) +
		", left " + std::to_string(left.files) + " file(s), " + std::to_string(left.bytes) + " bytes)");
	SharedFiles::Configure({}, false);
}

// Old Forge servers: the universal jar's manifest Class-Path, with 72-byte wrapped lines.
static void LegacyForgeServerChecks(const fs::path& dir)
{
	const fs::path jar = dir / "legacy-forge" / "forge-universal.jar";
	WriteJar(jar, { { "META-INF/MANIFEST.MF",
		"Manifest-Version: 1.0\r\n"
		"Main-Class: cpw.mods.fml.relauncher.ServerLaunchWrapper\r\n"
		"Class-Path: libraries/net/minecraft/launchwrapper/1.12/launchwrapper-1.\r\n"
		" 12.jar libraries/org/ow2/asm/asm-all/5.0.3/asm-all-5.0.3.jar ../evil.jar\r\n"
		"  minecraft_server.1.7.10.jar\r\n"
		"Tweak-Class: cpw.mods.fml.common.launcher.FMLTweaker\r\n" } });
	std::vector<std::string> classPath;
	std::string error;
	Check(ReadForgeServerClassPath(jar, classPath, &error), error);
	const std::vector<std::string> expected = {
		"libraries/net/minecraft/launchwrapper/1.12/launchwrapper-1.12.jar",
		"libraries/org/ow2/asm/asm-all/5.0.3/asm-all-5.0.3.jar",
		"minecraft_server.1.7.10.jar",
	};
	Check(classPath == expected, "Class-Path wasn't read as expected (" + std::to_string(classPath.size()) + " entries)");
	WriteJar(jar, { { "META-INF/MANIFEST.MF", "Manifest-Version: 1.0\n" } });
	Check(!ReadForgeServerClassPath(jar, classPath, &error), "A jar without a Class-Path was accepted");
	Check(!LegacyForgeLibraryMirrors().empty(), "No library mirrors");
}

// Jar mods: pack.json order, layering over the game jar, META-INF removal, and skipping rebuilds.
static void JarModChecks(const fs::path& dir)
{
	const fs::path install = dir / "jarmods-install";
	const fs::path game = install / "minecraft";
	const fs::path client = dir / "client.jar";
	WriteJar(client, { { "a/A.class", "vanilla A" }, { "b/B.class", "vanilla B" }, { "META-INF/MOJANG.SF", "sig" } });
	WriteJar(game / "jarmods" / "z-forge.zip", { { "b/B.class", "forge B" }, { "f/Forge.class", "forge" }, { "META-INF/MANIFEST.MF", "m" } });
	WriteJar(game / "jarmods" / "a-optifine.zip", { { "b/B.class", "optifine B" }, { "o/O.class", "optifine" } });
	WriteJar(game / "jarmods" / "extra.zip", { { "x/X.class", "extra" } });
	// The pack installs Forge first, then OptiFine; extra.zip isn't listed and goes last.
	Write(install / "pack.json", R"({"files":["mods/m.jar","jarmods/z-forge.zip","jarmods/a-optifine.zip"]})");
	const std::vector<fs::path> mods = FindJarMods(install, game);
	Check(mods.size() == 3 && mods[0].filename() == "z-forge.zip" && mods[1].filename() == "a-optifine.zip" &&
		mods[2].filename() == "extra.zip", "Jar mods should follow pack.json, then names");
	Check(FindJarMods(dir / "nothing", dir / "nothing").empty(), "No jarmods folder means no jar mods");

	const fs::path patched = game / ".mclp" / "patched-client.jar";
	std::string error;
	Check(BuildPatchedJar(client, mods, patched, &error), "Patching failed: " + error);
	mz_zip_archive zip{};
	Check(mz_zip_reader_init_file(&zip, patched.string().c_str(), 0) != 0, "Patched jar unreadable");
	auto entry = [&](const char* name) -> std::string {
		std::size_t size = 0;
		void* data = mz_zip_reader_extract_file_to_heap(&zip, name, &size, 0);
		if (!data)
			return "<missing>";
		std::string text(static_cast<const char*>(data), size);
		mz_free(data);
		return text;
	};
	const std::string a = entry("a/A.class"), b = entry("b/B.class"), f = entry("f/Forge.class"), o = entry("o/O.class"),
		x = entry("x/X.class"), sig = entry("META-INF/MOJANG.SF"), manifest = entry("META-INF/MANIFEST.MF");
	const mz_uint count = mz_zip_reader_get_num_files(&zip);
	mz_zip_reader_end(&zip);
	Check(a == "vanilla A" && b == "optifine B" && f == "forge" && o == "optifine" && x == "extra",
		"Later jar mods should override earlier ones (B=" + b + ")");
	Check(sig == "<missing>" && manifest == "<missing>" && count == 5, "META-INF should be left out");

	// Unchanged inputs: the jar isn't rebuilt (same write time). A changed mod rebuilds it.
	const auto built = fs::last_write_time(patched);
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	Check(BuildPatchedJar(client, mods, patched, &error) && fs::last_write_time(patched) == built, "Unchanged inputs shouldn't rebuild");
	WriteJar(game / "jarmods" / "extra.zip", { { "x/X.class", "extra v2" }, { "y/Y.class", "new" } });
	Check(BuildPatchedJar(client, mods, patched, &error), "Rebuild failed: " + error);
	mz_zip_archive again{};
	Check(mz_zip_reader_init_file(&again, patched.string().c_str(), 0) != 0 && mz_zip_reader_get_num_files(&again) == 6,
		"A changed jar mod should rebuild the patched jar");
	mz_zip_reader_end(&again);
	Check(!BuildPatchedJar(client, { game / "jarmods" / "missing.zip" }, dir / "bad.jar", &error) && !error.empty(),
		"A missing jar mod should fail with an error");

	// A Forge universal jar (it has a version.json) is a loader, not a jar mod.
	const std::string forgeJson = R"({"id":"1.6.4-Forge9.11.1.965","mainClass":"net.minecraft.launchwrapper.Launch",
		"minecraftArguments":"--tweakClass cpw.mods.fml.common.launcher.FMLTweaker","libraries":[
		{"name":"net.minecraftforge:minecraftforge:9.11.1.965"},
		{"name":"org.scala-lang:scala-library:2.10.2","url":"http://files.minecraftforge.net/maven/","checksums":["x"],"clientreq":true},
		{"name":"net.minecraft:launchwrapper:1.8"},
		{"name":"net.minecraft:server-only:1.0","clientreq":false}]})";
	const fs::path universal = game / "jarmods" / "forge-1.6.4-9.11.1.965-universal.jar";
	WriteJar(universal, { { "version.json", forgeJson }, { "cpw/Fml.class", "fml" } });
	const std::vector<fs::path> withForge = FindJarMods(install, game);
	Check(std::none_of(withForge.begin(), withForge.end(), [&](const fs::path& p) { return p.filename() == universal.filename(); }),
		"A jar with a version.json is a loader, not a jar mod");
	Check(IsLegacyForgeMinecraft("1.6.4") && IsLegacyForgeMinecraft("1.12.2") && !IsLegacyForgeMinecraft("1.13") &&
		!IsLegacyForgeMinecraft("1.20.1") && !IsLegacyForgeMinecraft("26.2"), "Legacy Forge versions");
	const fs::path root = dir / "legacy-forge-root";
	std::string versionId;
	Check(InstallForgeFromJar(root, universal, "1.6.4", versionId, &error), "Forge from its jar failed: " + error);
	const json written = json::parse(Read(root / "versions" / versionId / (versionId + ".json")));
	Check(versionId == "1.6.4-Forge9.11.1.965" && written.value("inheritsFrom", "") == "1.6.4" && written.value("jar", "") == "1.6.4",
		"Forge's version should inherit from vanilla");
	const json libs = written["libraries"];
	Check(libs.size() == 3 && libs[0]["downloads"]["artifact"].value("url", "x").empty() &&
		libs[1].value("url", "") == "https://maven.minecraftforge.net/" && !libs[1].contains("checksums") &&
		libs[2].value("name", "") == "net.minecraft:launchwrapper:1.8",
		"Forge library from the jar, moved maven URL, server-only library dropped");
	Check(NeedsLegacyJavaFixer("1.6.4", "9.11.1.965") && NeedsLegacyJavaFixer("1.7.2", "10.12.2.1161") &&
		NeedsLegacyJavaFixer("1.7.10", "10.13.0.1180") && !NeedsLegacyJavaFixer("1.7.10", "10.13.4.1614") &&
		!NeedsLegacyJavaFixer("1.12.2", "14.23.5.2860"), "LegacyJavaFixer only for Forge that needs it");
	Check(fs::is_regular_file(root / "libraries" / "net/minecraftforge/minecraftforge/9.11.1.965/minecraftforge-9.11.1.965.jar"),
		"The universal jar should be copied in as the Forge library");
}

// Manage > Content: listing, switching off and on, removing, and reading mod names from jars.
// Automatic Java: version parsing, finding Java homes in folders, and picking per Minecraft version.
static void JavaChecks(const fs::path& dir)
{
	Check(JavaMajorVersion("1.8.0_382") == 8 && JavaMajorVersion("17.0.8") == 17 && JavaMajorVersion("21") == 21 &&
		JavaMajorVersion("25-ea") == 25 && JavaMajorVersion("") == 0 && JavaMajorVersion("abc") == 0,
		"Java major versions parsed wrong");

	const fs::path javas = dir / "javas";
	auto fakeJava = [&](const std::string& folder, const std::string& release) {
		Write(javas / folder / "bin" / "java.exe", "stub");
		Write(javas / folder / "bin" / "javaw.exe", "stub");
		if (!release.empty())
			Write(javas / folder / "release", release);
	};
	fakeJava("jdk-17.0.2", "JAVA_VERSION=\"17.0.8\"\nIMPLEMENTOR=\"Eclipse Adoptium\"\nOS_ARCH=\"x86_64\"\n");
	fakeJava("jdk-17.0.10", "JAVA_VERSION=\"17.0.10\"\r\nOS_ARCH=\"amd64\"\r\n");
	fakeJava("jre1.8.0_381", "");                                   // no release file: the folder name
	fakeJava("vendor/java-runtime-delta", "");                        // a Mojang runtime one level down
	fakeJava("jdk17-32bit", "JAVA_VERSION=\"17.0.1\"\nOS_ARCH=\"x86\"\n"); // 32-bit: skipped
	Write(javas / "not-java" / "readme.txt", "no bin");

	const std::vector<JavaInstall> found = ScanJavaFolders({ javas });
	Check(found.size() == 4, "Expected 4 Java homes, found " + std::to_string(found.size()));
	const JavaInstall* java17 = PickJava(found, 17);
	Check(java17 && java17->version == "17.0.10" && java17->javaw.filename() == "javaw.exe",
		"Automatic Java didn't pick the newest Java 17");
	const JavaInstall* java8 = PickJava(found, 8);
	Check(java8 && java8->home.filename() == "jre1.8.0_381", "Automatic Java didn't find Java 8 by folder name");
	const JavaInstall* java21 = PickJava(found, 21);
	Check(java21 && java21->home.filename() == "java-runtime-delta" && java21->mojang && !java17->mojang,
		"Automatic Java didn't read a Mojang runtime's name");
	Check(!PickJava(found, 16), "Automatic Java picked a different major version");
	Check(JavaLabel(*PickJava({ found }, 17)) == "Java 17.0.10" &&
		JavaLabel(found.back()).rfind("Java ", 0) == 0, "Java labels are wrong");

	// The real PC: just make sure the search runs.
	const std::vector<JavaInstall> installed = FindJavaInstalls(dir / "game");
	std::cout << "Java on this PC:";
	for (const JavaInstall& java : installed)
		std::cout << " " << java.version;
	std::cout << (installed.empty() ? " none" : "") << "\n";
}

static void ContentChecks(const fs::path& dir)
{
	const fs::path game = dir / "content";
	WriteJar(game / "mods" / "fabric-mod.jar", { { "fabric.mod.json",
		R"({ "id": "sodium", "name": "Sodium", "version": "0.6.0", "description": " Fast. ", "authors": ["JellySquid", { "name": "IMS" }] })" } });
	WriteJar(game / "mods" / "forge-mod.jar.disabled", { { "META-INF/mods.toml",
		"modLoader=\"javafml\"\n[[mods]]\nmodId=\"jei\"\nversion=\"${file.jarVersion}\"\ndisplayName=\"Just Enough Items\"\n"
		"description='''\nItem lookup.\n'''\n[[dependencies.jei]]\nmodId=\"forge\"\n" },
		{ "META-INF/MANIFEST.MF", "Manifest-Version: 1.0\r\nImplementation-Version: 19.21.0\r\n" } });
	Write(game / "mods" / "notes.txt", "not a mod");
	Write(game / "resourcepacks" / "Faithful.zip", "zip");
	fs::create_directories(game / "resourcepacks" / "Unzipped Pack");

	std::vector<ContentFile> mods = ListContent(game, ContentKind::Mods);
	Check(mods.size() == 2 && mods[0].fileName == "fabric-mod.jar" && mods[0].enabled &&
		mods[1].fileName == "forge-mod.jar" && !mods[1].enabled, "Mods were not listed with their state");
	const std::vector<ContentFile> packs = ListContent(game, ContentKind::ResourcePacks);
	Check(packs.size() == 2 && packs[1].folder, "Resource pack files and folders were not listed");

	ModMetadata meta;
	Check(ReadModMetadata(game / "mods" / "fabric-mod.jar", meta) && meta.name == "Sodium" && meta.version == "0.6.0" &&
		meta.description == "Fast." && meta.loader == "Fabric" && meta.authors.size() == 2 && meta.authors[1] == "IMS",
		"fabric.mod.json was not read");
	Check(ReadModMetadata(game / "mods" / "forge-mod.jar.disabled", meta) && meta.name == "Just Enough Items" &&
		meta.version == "19.21.0" && meta.description == "Item lookup." && meta.loader == "Forge", "mods.toml was not read");
	Check(!ReadModMetadata(game / "mods" / "notes.txt", meta), "A non-jar had metadata");

	std::string error;
	Check(SetContentEnabled(game / "mods", "forge-mod.jar", true, &error), error);
	Check(fs::exists(game / "mods" / "forge-mod.jar") && !fs::exists(game / "mods" / "forge-mod.jar.disabled"), "Mod was not enabled");
	Check(SetContentEnabled(game / "resourcepacks", "Unzipped Pack", false, &error) &&
		fs::is_directory(game / "resourcepacks" / "Unzipped Pack.disabled"), "Pack folder was not disabled");
	Check(!SetContentEnabled(game / "mods", "../escape.jar", false, &error), "Unsafe name was accepted");
	Check(DeleteContent(game / "resourcepacks", "Unzipped Pack", &error) &&
		!fs::exists(game / "resourcepacks" / "Unzipped Pack.disabled"), "Disabled pack folder was not deleted");
}

// Adding files to a pack and keeping track of the player's changes.
static void PackEditChecks(const fs::path& dir)
{
	ContentChanges changes;
	RecordContentChange(changes, "mods/mine.jar", ContentChange::Added);
	RecordContentChange(changes, "mods/mine.jar", ContentChange::Disabled); // still the player's own file
	RecordContentChange(changes, "mods/pack.jar", ContentChange::Removed);
	RecordContentChange(changes, "mods/heavy.jar", ContentChange::Disabled);
	RecordContentChange(changes, "mods/optional.jar", ContentChange::Enabled);
	Check(changes.size() == 4 && changes["mods/mine.jar"] == "added" &&
		DescribeContentChanges(changes) == "1 added, 1 removed, 1 turned off, 1 turned on",
		"Changes were not recorded: " + DescribeContentChanges(changes));
	Check(RemovedContent(changes) == std::set<std::string>{ "mods/pack.jar" }, "Removed files were not listed");
	// Changes that cancel out leave nothing.
	ContentChanges undone = changes;
	RecordContentChange(undone, "mods/mine.jar", ContentChange::Removed);
	RecordContentChange(undone, "mods/pack.jar", ContentChange::Added);
	RecordContentChange(undone, "mods/heavy.jar", ContentChange::Enabled);
	RecordContentChange(undone, "mods/optional.jar", ContentChange::Disabled);
	Check(undone.empty() && DescribeContentChanges(undone).empty(), "Changes that cancel out were kept");

	// Adding files.
	const fs::path game = dir / "edit" / "game";
	const fs::path picked = dir / "edit" / "picked";
	Write(picked / "mine.jar", "mine");
	Write(picked / "Sky.zip", "zip");
	Write(picked / "readme.txt", "text");
	fs::create_directories(picked / "Folder Pack");
	ContentKind kind;
	Check(ContentKindForFile(picked / "mine.jar", ContentKind::ShaderPacks, &kind) && kind == ContentKind::Mods &&
		ContentKindForFile(picked / "Sky.zip", ContentKind::ShaderPacks, &kind) && kind == ContentKind::ShaderPacks &&
		ContentKindForFile(picked / "Sky.zip", ContentKind::Mods, &kind) && kind == ContentKind::ResourcePacks &&
		ContentKindForFile(picked / "Folder Pack", ContentKind::Mods, &kind) && kind == ContentKind::ResourcePacks &&
		!ContentKindForFile(picked / "readme.txt", ContentKind::Mods, &kind), "Dropped files went to the wrong folder");
	std::string name, error;
	Check(AddContent(game, ContentKind::Mods, picked / "mine.jar", &name, &error) && name == "mine.jar" &&
		Read(game / "mods/mine.jar") == "mine", "A mod wasn't added: " + error);
	Check(!AddContent(game, ContentKind::Mods, picked / "mine.jar", &name, &error) &&
		error.find("already") != std::string::npos, "A mod was added twice");
	Check(!AddContent(game, ContentKind::Mods, picked / "Sky.zip", &name, &error), "A zip was added as a mod");
	Check(AddContent(game, ContentKind::ResourcePacks, picked / "Folder Pack", &name, &error) &&
		fs::is_directory(game / "resourcepacks/Folder Pack"), "A resource pack folder wasn't added: " + error);

	// Re-applying changes after the pack's files come back (an update or repair), then undoing them.
	Write(game / "mods/pack.jar", "pack");
	Write(game / "mods/heavy.jar", "pack");
	Write(game / "mods/heavy.jar.disabled", "pack"); // an update re-added a mod the player had turned off
	Write(game / "mods/optional.jar.disabled", "pack");
	ApplyContentChanges(game, changes);
	Check(!fs::exists(game / "mods/pack.jar") && !fs::exists(game / "mods/heavy.jar") &&
		fs::exists(game / "mods/heavy.jar.disabled") && fs::exists(game / "mods/optional.jar") &&
		fs::exists(game / "mods/mine.jar"), "The player's changes were not re-applied");
	Check(UndoContentChanges(game, changes, &error), error);
	Check(!fs::exists(game / "mods/mine.jar") && fs::exists(game / "mods/heavy.jar") &&
		fs::exists(game / "mods/optional.jar.disabled"), "The player's changes were not undone");

	// Saved per installed version, and kept when the version is installed again.
	Library library;
	Modpack pack;
	pack.id = "edited";
	pack.name = "Edited";
	pack.version = "1.0";
	InstalledVersion version = PlanInstalledVersion(library, pack, false);
	version.contentChanges = changes;
	AddInstalledVersion(library, pack, version);
	const fs::path libraryFile = dir / "edit" / "library.json";
	Check(SavePackLibrary(libraryFile.string(), library), "Library save failed");
	const Library loaded = LoadPackLibrary(libraryFile.string());
	Check(loaded.count("edited") && ActiveInstalledVersion(loaded.at("edited")) &&
		ActiveInstalledVersion(loaded.at("edited"))->contentChanges == changes, "Changes did not round-trip through library.json");
	InstalledVersion again = PlanInstalledVersion(library, pack, false);
	Check(again.contentChanges == changes, "Reinstalling forgot the player's changes");
	again.contentChanges.clear();
	AddInstalledVersion(library, pack, again);
	Check(ActiveInstalledVersion(library.at("edited"))->contentChanges == changes, "Replacing a version dropped its changes");

	// Total play time is saved with the pack and survives installing another version.
	library.at("edited").playSeconds = 11100;
	Check(SavePackLibrary(libraryFile.string(), library), "Library save failed");
	Check(LoadPackLibrary(libraryFile.string()).at("edited").playSeconds == 11100, "Play time did not round-trip through library.json");
	AddInstalledVersion(library, pack, PlanInstalledVersion(library, pack, false));
	Check(library.at("edited").playSeconds == 11100, "Installing a version reset the play time");
	Check(FormatShortDate("2026-10-07T12:00:00Z") == "Oct 7, 2026" && FormatShortDate("2025-01-31") == "Jan 31, 2025" &&
		FormatShortDate("").empty() && FormatShortDate("yesterday").empty() && FormatShortDate("2026-13-01").empty(),
		"Short dates were formatted wrongly");
	Check(IsoDateFromEpoch(0).empty() && IsoDateFromEpoch(1760000000) == "2025-10-09", "An epoch time was formatted wrongly");
	Check(CategoryLabel("adventure-and-rpg") == "Adventure And Rpg" && CategoryLabel("technology") == "Technology",
		"A category slug was formatted wrongly");
	Check(FormatPlaytime(0) == "Not played yet" && FormatPlaytime(45) == "Under a minute" && FormatPlaytime(12 * 60) == "12 min" &&
		FormatPlaytime(11100) == "3 h 05 min" && FormatPlaytime(120 * 3600) == "120 h", "Play time was formatted wrongly");
}

// New packs made in the launcher.
static void PackCreatorChecks(const fs::path& dir)
{
	Check(CustomPackId("My Cool Pack!", {}) == "custom-my-cool-pack" &&
		CustomPackId("My Cool Pack", { "custom-my-cool-pack" }) == "custom-my-cool-pack-2" &&
		CustomPackId("  ***  ", {}) == "custom-pack", "Custom pack ids were not made safely");
	Check(NeoForgeVersionPrefix("1.21.1") == "21.1." && NeoForgeVersionPrefix("1.21") == "21.0." &&
		NeoForgeVersionPrefix("26.1") == "26.1.", "NeoForge version prefixes were wrong");
	Check(PackSourceFromKey("custom") == PackSource::Custom && std::string(PackSourceName(PackSource::Custom)) == "Custom",
		"The custom source didn't round-trip");
	const fs::path installDir = dir / "created" / "custom-test@1.0";
	std::string error;
	Check(WriteCustomPack(installDir, "1.0", "1.20.1", "fabric", "0.16.9", &error), error);
	InstalledPack pack;
	Check(IsWholePackInstall(installDir) && ReadInstalledPack(installDir, pack, &error) && pack.minecraftVersion == "1.20.1" &&
		pack.loader == LoaderKind::Fabric && pack.loaderVersion == "0.16.9" && pack.version == "1.0" &&
		fs::is_directory(installDir / "minecraft/mods") && fs::is_directory(installDir / "minecraft/shaderpacks"),
		"A new pack couldn't be read back: " + error);
	Check(CanCreateServer(installDir), "A new pack can't be hosted");
	const fs::path vanillaDir = dir / "created" / "custom-vanilla@1.0";
	Check(WriteCustomPack(vanillaDir, "1.0", "1.21.1", "", "", &error) && ReadInstalledPack(vanillaDir, pack, &error) &&
		pack.loader == LoaderKind::Vanilla, "A vanilla pack couldn't be read back: " + error);
}

// Counts the GETs it answers, optionally slowly, and fails the URLs listed in failing.
class CountingHttp final : public IHttpClient {
public:
	std::atomic<int> gets{ 0 };
	std::atomic<int> posts{ 0 };
	int delayMs = 0;
	bool fail = false;
	std::string reply = "v1";

	bool DownloadToFile(const std::string&, const std::string&, std::function<void(double, double)>, std::string*,
		const std::atomic<bool>*, const std::vector<std::string>&) override { return true; }
	bool Get(const std::string& url, std::string& body, std::string* err, const std::atomic<bool>*,
		const std::vector<std::string>&) override
	{
		++gets;
		if (delayMs > 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
		if (fail) {
			if (err) *err = "connection refused";
			return false;
		}
		body = reply + ":" + url;
		return true;
	}
	bool Post(const std::string&, const std::string&, std::string& body, std::string*, const std::atomic<bool>*,
		const std::vector<std::string>&) override { ++posts; body = "posted"; return true; }
	bool Request(const std::string&, const std::vector<std::string>&, const std::string&, std::string&, long& status,
		std::string*, const std::atomic<bool>*) override { status = 200; return true; }
};

static void CachingChecks()
{
	double now = 0.0;
	CountingHttp origin;
	CachingHttpClient::Limits limits;
	limits.lifetimeSeconds = 60.0;
	limits.maxEntryBytes = 64;
	limits.maxTotalBytes = 200;
	CachingHttpClient cache(origin, limits, [&now] { return now; });
	std::string body, error;

	// A repeat inside the lifetime is answered from memory; a different URL or header is a different entry.
	Check(cache.Get("https://api.test/a", body, &error) && body == "v1:https://api.test/a" && origin.gets == 1, "The first GET should go out");
	body.clear();
	Check(cache.Get("https://api.test/a", body, &error) && body == "v1:https://api.test/a" && origin.gets == 1,
		"A repeat GET should come from the cache");
	Check(cache.Get("https://api.test/b", body) && cache.Get("https://api.test/a", body, nullptr, nullptr, { "x-api-key: k" }) && origin.gets == 3,
		"Another URL or header set should be its own entry");
	Check(cache.GetStats().hits == 1 && cache.GetStats().requests == 3, "The counters should match");

	// Time moves on: the entry expires, and the new answer replaces it.
	origin.reply = "v2";
	now = 59.0;
	cache.Get("https://api.test/a", body);
	Check(body == "v1:https://api.test/a" && origin.gets == 3, "An entry should be served until its lifetime ends");
	now = 61.0;
	cache.Get("https://api.test/a", body);
	Check(body == "v2:https://api.test/a" && origin.gets == 4, "An expired entry should be fetched again");

	// Post, Request and downloads are never cached.
	std::string posted;
	cache.Post("https://api.test/p", "{}", posted);
	cache.Post("https://api.test/p", "{}", posted);
	Check(origin.posts == 2, "POSTs should always go out");

	// Failures are not remembered.
	origin.fail = true;
	Check(!cache.Get("https://api.test/bad", body, &error) && error == "connection refused", "A failure should reach the caller");
	origin.fail = false;
	const int before = origin.gets;
	Check(cache.Get("https://api.test/bad", body) && origin.gets == before + 1, "A failed GET should be retried, not cached");

	// Clear() is the Refresh button.
	const int beforeClear = origin.gets;
	cache.Clear();
	Check(cache.EntryCount() == 0, "Clear should empty the cache");
	cache.Get("https://api.test/a", body);
	Check(origin.gets == beforeClear + 1, "After Clear the data should be fetched again");

	// Size limits: an oversized answer is returned but not kept; the oldest entries make room.
	origin.reply = std::string(100, 'x');
	cache.Clear();
	const int bigBefore = origin.gets;
	Check(cache.Get("https://api.test/big", body) && body.size() > 64, "A big response should still be returned");
	cache.Get("https://api.test/big", body);
	Check(origin.gets == bigBefore + 2, "A response over the entry limit should not be cached");
	origin.reply = std::string(30, 'y');
	cache.Clear();
	for (int i = 0; i < 10; ++i) {
		now += 1.0;
		cache.Get("https://api.test/many" + std::to_string(i), body);
	}
	Check(cache.EntryCount() < 10 && cache.EntryCount() > 0, "The total size limit should evict old entries");
	const int newestBefore = origin.gets;
	cache.Get("https://api.test/many9", body);
	Check(origin.gets == newestBefore, "The newest entry should survive eviction");

	// Identical GETs at the same moment share one request.
	CountingHttp slow;
	slow.delayMs = 200;
	CachingHttpClient shared(slow);
	std::vector<std::thread> threads;
	std::atomic<int> succeeded{ 0 };
	for (int i = 0; i < 6; ++i) {
		threads.emplace_back([&] {
			std::string text;
			if (shared.Get("https://api.test/same", text) && text == "v1:https://api.test/same")
				++succeeded;
		});
	}
	for (std::thread& thread : threads)
		thread.join();
	Check(succeeded == 6 && slow.gets == 1, "Six identical concurrent GETs should send one request, sent " + std::to_string(slow.gets.load()));

	// A Clear() while a request is on its way must not let the old answer in.
	CountingHttp racing;
	racing.delayMs = 200;
	CachingHttpClient raced(racing);
	std::thread worker([&] { std::string text; raced.Get("https://api.test/race", text); });
	std::this_thread::sleep_for(std::chrono::milliseconds(60));
	raced.Clear();
	worker.join();
	Check(raced.EntryCount() == 0, "A response that was in flight during Clear should not be cached");

	// A waiter can be cancelled without waiting for the other request.
	CountingHttp waiting;
	waiting.delayMs = 400;
	CachingHttpClient cancelled(waiting);
	std::thread leader([&] { std::string text; cancelled.Get("https://api.test/slow", text); });
	std::this_thread::sleep_for(std::chrono::milliseconds(60));
	std::atomic<bool> stop{ true };
	std::string cancelError;
	Check(!cancelled.Get("https://api.test/slow", body, &cancelError, &stop) && cancelError == "Cancelled", "A cancelled waiter should give up");
	leader.join();
}

// Answers every GET with one fixed body and counts them.
class BodyHttp final : public IHttpClient {
public:
	std::string reply;
	int gets = 0;

	bool DownloadToFile(const std::string&, const std::string&, std::function<void(double, double)>, std::string*,
		const std::atomic<bool>*, const std::vector<std::string>&) override { return true; }
	bool Get(const std::string&, std::string& body, std::string*, const std::atomic<bool>*,
		const std::vector<std::string>&) override { ++gets; body = reply; return true; }
	bool Post(const std::string&, const std::string&, std::string&, std::string*, const std::atomic<bool>*,
		const std::vector<std::string>&) override { return true; }
	bool Request(const std::string&, const std::vector<std::string>&, const std::string&, std::string&, long&, std::string*,
		const std::atomic<bool>*) override { return true; }
};

// A .mrpack (a zip) whose modrinth.index.json is indexJson.
static std::string MakeMrpack(const std::string& indexJson)
{
	mz_zip_archive zip{};
	Check(mz_zip_writer_init_heap(&zip, 0, 0), "A zip should start in memory");
	Check(mz_zip_writer_add_mem(&zip, "modrinth.index.json", indexJson.data(), indexJson.size(), MZ_NO_COMPRESSION), "The index should be added");
	void* buffer = nullptr;
	std::size_t size = 0;
	Check(mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size), "The zip should finish");
	mz_zip_writer_end(&zip);
	std::string bytes(static_cast<const char*>(buffer), size);
	mz_free(buffer);
	return bytes;
}

static void PackSizeChecks()
{
	BodyHttp http;
	Modpack pack;
	std::uint64_t bytes = 0;
	bool partial = false;
	std::string error;

	// The download is the .mrpack plus the client files its index lists; server-only files don't count.
	http.reply = MakeMrpack(R"({"files":[{"fileSize":1000,"env":{"client":"required"}},{"fileSize":2000},{"fileSize":5000,"env":{"client":"unsupported"}}]})");
	pack.files.push_back(PackFile{ "p.mrpack", "https://cdn.test/p.mrpack", http.reply.size() });
	Check(FetchModrinthPackSize(http, pack, bytes, partial, &error) && bytes == http.reply.size() + 3000 && !partial,
		"A pack's size should be its .mrpack plus its client files: " + std::to_string(bytes));

	// A file without a size makes the total a lower bound.
	http.reply = MakeMrpack(R"({"files":[{"fileSize":1000},{"downloads":[]}]})");
	pack.files[0].size = http.reply.size();
	Check(FetchModrinthPackSize(http, pack, bytes, partial, &error) && bytes == http.reply.size() + 1000 && partial,
		"A missing file size should mark the total as partial");

	// A big .mrpack isn't fetched at all.
	http.gets = 0;
	pack.files[0].size = 50u * 1024u * 1024u;
	Check(FetchModrinthPackSize(http, pack, bytes, partial, &error) && bytes == pack.files[0].size && partial && http.gets == 0,
		"A big .mrpack should be reported by its own size without being downloaded");

	// Not a zip, and no files at all.
	http.reply = "not a zip";
	pack.files[0].size = 9;
	Check(!FetchModrinthPackSize(http, pack, bytes, partial, &error) && !error.empty(), "A broken .mrpack should fail with a message");
	Modpack empty;
	Check(!FetchModrinthPackSize(http, empty, bytes, partial, &error), "A pack without files can't be sized");
}

// An HTTP client that answers from memory, for the tracing checks.
class FakeHttp final : public IHttpClient {
public:
	bool fail = false;

	bool DownloadToFile(const std::string&, const std::string& outPath, std::function<void(double, double)>, std::string* err,
		const std::atomic<bool>*, const std::vector<std::string>&) override
	{
		if (fail) {
			if (err) *err = "connection refused";
			return false;
		}
		Write(outPath, "12345");
		return true;
	}
	bool Get(const std::string&, std::string& body, std::string* err, const std::atomic<bool>*,
		const std::vector<std::string>&) override
	{
		if (fail) {
			if (err) *err = "connection refused";
			return false;
		}
		body = "{\"hello\":\n\"world\"}";
		return true;
	}
	bool Post(const std::string&, const std::string&, std::string& body, std::string*, const std::atomic<bool>*,
		const std::vector<std::string>&) override
	{
		body = "posted";
		return true;
	}
	bool Request(const std::string&, const std::vector<std::string>&, const std::string&, std::string& responseBody, long& status,
		std::string*, const std::atomic<bool>*) override
	{
		responseBody = "{}";
		status = 403;
		return true;
	}
};

static bool Contains(const std::string& text, const std::string& part)
{
	return text.find(part) != std::string::npos;
}

static void FramePacingChecks()
{
	using FramePacing::Choose;
	using FramePacing::Input;
	Input input;
	// In use, no cap: no sleeping, vsync paces the loop.
	Check(Choose(input).interval == 0.0 && !Choose(input).idle, "An active window with no cap should not be paced");
	input.maxFps = 60;
	Check(std::abs(Choose(input).interval - 1.0 / 60.0) < 1e-9, "An active window should follow the cap");
	// Idle for a second: drop to the idle rate, and wake on any event.
	input.idleSeconds = FramePacing::kActiveSeconds - 0.01;
	Check(!Choose(input).idle, "Just under the active time should still count as active");
	input.idleSeconds = FramePacing::kActiveSeconds;
	Check(Choose(input).idle && std::abs(Choose(input).interval - 1.0 / FramePacing::kIdleFps) < 1e-9, "An idle window should draw at the idle rate");
	input.maxFps = 15;
	Check(std::abs(Choose(input).interval - 1.0 / 15.0) < 1e-9, "The idle rate must never exceed the cap");
	input.maxFps = 0;
	// Background and minimized.
	input.focused = false;
	input.idleSeconds = 0.0;
	Check(std::abs(Choose(input).interval - 1.0 / FramePacing::kBackgroundFps) < 1e-9, "An unfocused window should draw at the background rate even right after input");
	input.focused = true;
	input.minimized = true;
	Check(std::abs(Choose(input).interval - 1.0 / FramePacing::kBackgroundFps) < 1e-9, "A minimized window should draw at the background rate");
	// With the idle option off, only minimized windows are slowed down.
	input = Input();
	input.lowerWhenIdle = false;
	input.idleSeconds = 60.0;
	input.focused = false;
	Check(Choose(input).interval == 0.0 && !Choose(input).idle, "With the idle option off an idle, unfocused window should keep the cap");
	input.minimized = true;
	Check(std::abs(Choose(input).interval - 1.0 / FramePacing::kBackgroundFps) < 1e-9, "A minimized window should be slowed even with the idle option off");
}

static void DebugTraceChecks(const fs::path& dir, const fs::path& executable)
{
	// Channel lists.
	unsigned mask = 0;
	std::string unknown;
	Check(Debug::ParseChannels("http, FILE", mask, &unknown) && mask == (Debug::kHttp | Debug::kFile) && unknown.empty(),
		"Channel names should be read case-insensitively");
	mask = 0;
	Check(Debug::ParseChannels("all", mask) && mask == Debug::kAll && !(mask & Debug::kHttpBody),
		"\"all\" should not include response bodies");
	Check(Debug::ParseChannels("body", mask) && (mask & Debug::kHttpBody), "The body channel should be selectable");
	mask = 0;
	Check(!Debug::ParseChannels("bogus,process", mask, &unknown) && unknown == "bogus" && mask == Debug::kProcess,
		"An unknown channel should be reported and the known ones kept");
	Check(Debug::ChannelNames(Debug::kHttp | Debug::kLog) == "http,log", "Channel names should list the channels in order");
	mask = 0;
	Check(Debug::ParseChannels("perf", mask) && mask == Debug::kPerf && !(Debug::kAll & Debug::kPerf),
		"The perf channel should be selectable and not part of \"all\"");

	// What never reaches the console.
	Check(Debug::RedactUrl("https://x.test/a?key=abc&q=1&access_token=zz") == "https://x.test/a?key=***&q=1&access_token=***",
		"Secret query values should be hidden: " + Debug::RedactUrl("https://x.test/a?key=abc&q=1&access_token=zz"));
	Check(Debug::RedactUrl("https://x.test/a") == "https://x.test/a", "A URL without a query should be left alone");
	const std::string command = Debug::DescribeCommand({ "java", "--accessToken", "SECRET", "--version", "1.2", "a b" });
	Check(!Contains(command, "SECRET") && Contains(command, "--accessToken ***") && Contains(command, "--version 1.2") &&
		Contains(command, "\"a b\""), "A command line should hide token values: " + command);
	Check(Debug::PathText("\\\\?\\C:\\a\\b") == "C:\\a\\b", "The long-path prefix should be dropped");

	std::ostringstream sink;
	constexpr unsigned kEverything = Debug::kAll | Debug::kHttpBody;
	Debug::Configure(kEverything, &sink);

	// File operations.
	const fs::path root = dir / "trace";
	std::error_code ec;
	TracedFs::CreateDirectories(root / "a", ec);
	Write(root / "a" / "one.txt", "x");
	TracedFs::CopyFile(root / "a" / "one.txt", root / "a" / "two.txt", ec);
	TracedFs::Rename(root / "a" / "two.txt", root / "a" / "three.txt", ec);
	TracedFs::CreateHardLink(root / "a" / "one.txt", root / "a" / "link.txt", ec);
	TracedFs::Remove(root / "a" / "three.txt", ec);
	TracedFs::RemoveAll(root / "a", ec);
	TracedFs::NoteWrite(root / "saved.json", 42);
	const std::string files = sink.str();
	for (const char* part : { "[FILE ]", "mkdir ", "copy ", "rename ", "hard-link ", "remove ", "remove-all ", "write ", "(42 bytes)" })
		Check(Contains(files, part), std::string("The file trace should contain \"") + part + "\":\n" + files);
	Check(!fs::exists(root / "a"), "TracedFs should still do the operation");
	sink.str("");
	TracedFs::Remove(root / "missing.txt", ec);
	Check(sink.str().empty(), "Removing nothing should not be traced");
	TracedFs::CopyFile(root / "missing.txt", root / "x.txt", ec);
	Check(Contains(sink.str(), "FAILED"), "A failed operation should say so");

	// app.log lines.
	sink.str("");
	std::ostringstream target;
	{
		Debug::LogTee tee(target.rdbuf());
		std::ostream log(&tee);
		log << "Loaded " << 3 << " pack(s)\n" << "partial";
		log.flush();
	}
	Check(target.str() == "Loaded 3 pack(s)\npartial", "The log tee should pass everything on");
	Check(Contains(sink.str(), "[LOG  ]") && Contains(sink.str(), "Loaded 3 pack(s)") && !Contains(sink.str(), "partial"),
		"The log tee should trace each finished line:\n" + sink.str());

	// Offline mode: every call fails at once with the offline message, and the trace shows it.
	{
		char programName[] = "app";
		char offlineFlag[] = "--offline";
		char* withFlag[] = { programName, offlineFlag };
		char* withoutFlag[] = { programName };
		Check(Debug::OfflineRequested(2, withFlag), "--offline should turn offline mode on");
		Check(!Debug::OfflineRequested(1, withoutFlag) || std::getenv("MCLP_OFFLINE"), "Offline mode should be off by default");
		OfflineHttpClient offline;
		TracingHttpClient tracedOffline(offline);
		sink.str("");
		std::string offlineBody, offlineError;
		long status = 123;
		Check(!tracedOffline.Get("https://api.test/v1/x", offlineBody, &offlineError) && offlineError == OfflineHttpClient::kMessage &&
			offlineBody.empty(), "Offline Get should fail with the offline message");
		Check(!tracedOffline.Post("https://api.test/v1/x", "{}", offlineBody, &offlineError) &&
			!tracedOffline.DownloadToFile("https://api.test/f.zip", (dir / "never.zip").string(), nullptr, &offlineError) &&
			!tracedOffline.Request("https://api.test/v1/x", {}, "", offlineBody, status, &offlineError) && status == 0 &&
			!fs::exists(dir / "never.zip"), "Offline Post, DownloadToFile and Request should fail without writing anything");
		Check(Contains(sink.str(), "FAILED") && Contains(sink.str(), "Offline mode"), "The trace should show the blocked request:\n" + sink.str());
	}

	// HTTP.
	FakeHttp fake;
	TracingHttpClient http(fake);
	sink.str("");
	std::string body, error;
	Check(http.Get("https://api.test/v1/x?key=secret123&q=1", body, &error, nullptr, { "x-api-key: secret123" }) && body.find("world") != std::string::npos,
		"The tracing client should return what the real one does");
	std::string trace = sink.str();
	Check(Contains(trace, "GET  https://api.test/v1/x?key=***&q=1 [x-api-key]") && Contains(trace, "<- GET ok") && Contains(trace, "ms"),
		"A GET should be traced:\n" + trace);
	Check(!Contains(trace, "secret123"), "Secrets must not appear in the trace:\n" + trace);
	Check(Contains(trace, "[BODY ]") && Contains(trace, "{\"hello\": \"world\"}"), "The body channel should show the response:\n" + trace);
	sink.str("");
	long status = 0;
	Check(http.Request("https://login.test/token", { "Authorization: Bearer abc" }, "grant=secret", body, status) && status == 403,
		"Request should pass the status through");
	trace = sink.str();
	Check(Contains(trace, "POST https://login.test/token [Authorization] (12 bytes sent)") && Contains(trace, "<- HTTP 403") &&
		!Contains(trace, "abc") && !Contains(trace, "grant"), "A request should be traced without its secrets:\n" + trace);
	sink.str("");
	const fs::path downloaded = root / "file.bin";
	Check(http.DownloadToFile("https://cdn.test/f.bin", downloaded.string()), "A download should pass through");
	Check(Contains(sink.str(), "DOWNLOAD https://cdn.test/f.bin") && Contains(sink.str(), "<- DOWNLOAD ok, 5 bytes"),
		"A download should be traced with its size:\n" + sink.str());
	sink.str("");
	fake.fail = true;
	Check(!http.Get("https://api.test/down", body, &error) && error == "connection refused", "A failure should reach the caller");
	Check(Contains(sink.str(), "<- GET FAILED: connection refused"), "A failed GET should be traced:\n" + sink.str());

	// Child processes: the command, then the exit code, with token values hidden.
#ifdef _WIN32
	sink.str("");
	int exitCode = -1;
	Check(RunProcess({ executable.string(), "-jar", "--accessToken", "SECRET" }, dir, dir / "proc.log", true, &exitCode) && exitCode == 0,
		"The fixture process should run");
	trace = sink.str();
	Check(Contains(trace, "[PROC ]") && Contains(trace, "start  ") && Contains(trace, "--accessToken ***") && Contains(trace, "exit 0") &&
		!Contains(trace, "SECRET"), "A process should be traced without its tokens:\n" + trace);
#endif

	// Off: nothing is written and the calls still work.
	Debug::Configure(0, &sink);
	sink.str("");
	fake.fail = false;
	Check(http.Get("https://api.test/quiet", body) && sink.str().empty(), "Nothing should be traced with the harness off");
	TracedFs::CreateDirectories(root / "quiet", ec);
	Check(sink.str().empty() && fs::exists(root / "quiet"), "File operations should be silent with the harness off");
	Debug::Configure(0, nullptr);
}

int main(int argc, char** argv)
{
	if (argc > 1 && std::string(argv[1]) == "-jar")
		return 0; // cached Forge installer fixture
	if (argc > 2 && std::string(argv[1]) == "--sleep-fixture") {
		std::this_thread::sleep_for(std::chrono::seconds(std::stoi(argv[2])));
		return 0;
	}
	if (argc > 2 && std::string(argv[1]) == "--console-fixture") {
		std::cout << argv[2] << std::endl;
		std::string line;
		while (std::getline(std::cin, line)) {
			std::cout << "received:" << line << std::endl;
			if (line == "spawn") // a long-running child, like a program a server starts itself
				RunProcess({ argv[0], "--sleep-fixture", "8" }, fs::current_path(), fs::current_path() / "child.log", false,
					nullptr, nullptr);
			if (line == "stop") return 0;
		}
		return 1;
	}
	try {
		const fs::path dir = fs::absolute(fs::path("tests/out") /
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(dir);
		const fs::path executable = fs::absolute(argv[0]);
		SettingsChecks(dir);
		ServerChecks(dir, executable);
		WholePackServerChecks(dir, executable);
		DockerChecks(dir);
		ServerRemoveChecks(dir);
		PlayerDataChecks(dir);
		TextChecks();
		CardChecks();
		ProcessChecks(dir, executable);
		ContentChecks(dir);
		PackEditChecks(dir);
		PackCreatorChecks(dir);
		JavaChecks(dir);
		JarModChecks(dir);
		LegacyForgeServerChecks(dir);
		SharedFileChecks(dir);
		DebugTraceChecks(dir, executable);
		FramePacingChecks();
		CachingChecks();
		PackSizeChecks();
		std::cout << "Window settings, server setup, cards and console checks passed\n";
		return 0;
	} catch (const std::exception& ex) {
		std::cerr << ex.what() << '\n';
		return 1;
	}
}

#include "Game/InstanceContent.h"
#include "Game/JavaFinder.h"
#include "Game/PackCreator.h"
#include "Game/PackInstance.h"
#include "Game/ServerSetup.h"
#include "Network/HttpClient.h"
#include "Platform.h"
#include "Storage/Library.h"
#include "Storage/Settings.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Paths.h"
#include "Util/Process.h"
#include "Util/Sha1.h"
#include "Util/Text.h"
#include "imgui_internal.h"

#include <miniz/miniz.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
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
		std::cout << "Window settings, server setup, cards and console checks passed\n";
		return 0;
	} catch (const std::exception& ex) {
		std::cerr << ex.what() << '\n';
		return 1;
	}
}

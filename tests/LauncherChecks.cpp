#include "Game/PackInstance.h"
#include "Game/ServerSetup.h"
#include "Network/HttpClient.h"
#include "Platform.h"
#include "Storage/Settings.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Paths.h"
#include "Util/Process.h"
#include "Util/Sha1.h"
#include "Util/Text.h"
#include "imgui_internal.h"

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
	Check(SaveSettings(path.string(), settings), "Settings save failed");
	const Settings restored = LoadSettings(path.string());
	Check(restored.window.saved && restored.window.x == -1400 && restored.window.y == 120 &&
		restored.window.width == 1280 && restored.window.height == 760 && restored.window.maximized,
		"Window placement did not round-trip");
	Check(restored.discoverSources == settings.discoverSources, "Discover filters did not round-trip");
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
	ServerInfo info;
	Check(SetupServer(http, request, info, {}, &error, nullptr), error);
	Check(Read(LongPath(fs::absolute(request.serverDir / deepOverride / "deep.cfg"))) == "deep", "Deep override was not copied");
	Check(Read(request.serverDir / "config/shared.txt") == "server", "Server override did not win");
	Check(!fs::exists(request.serverDir / "client.txt") && !fs::exists(request.serverDir / "mods/client.jar"),
		"Client files leaked into server");
	Check(LoadServerInfo(request.serverDir, info) && info.launchArgs == std::vector<std::string>({ "-jar", "server.jar", "nogui" }),
		"Server launch info was not saved");
	Check(ServerCommand(info, 6144)[1] == "-Xmx6144M", "Server memory was not applied");
	Check(!EulaAccepted(request.serverDir), "EULA was accepted without consent");
	Check(AcceptEula(request.serverDir, &error) && EulaAccepted(request.serverDir), "EULA acceptance failed");
	Write(request.serverDir / "server.properties", "# keep me\ncustom-setting=yes\nserver-port=25565\n");
	ServerProperties props;
	props.port = 25570;
	props.onlineMode = false;
	props.serverIp = "100.64.1.2";
	Check(WriteServerProperties(request.serverDir, props, &error), "Properties write failed");
	Check(ReadServerProperties(request.serverDir).port == 25570 && !ReadServerProperties(request.serverDir).onlineMode &&
		ReadServerProperties(request.serverDir).serverIp == "100.64.1.2", "Properties did not round-trip");
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
#endif
}

int main(int argc, char** argv)
{
	if (argc > 1 && std::string(argv[1]) == "-jar")
		return 0; // cached Forge installer fixture
	if (argc > 2 && std::string(argv[1]) == "--console-fixture") {
		std::cout << argv[2] << std::endl;
		std::string line;
		while (std::getline(std::cin, line)) {
			std::cout << "received:" << line << std::endl;
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
		std::cout << "Window settings, server setup, cards and console checks passed\n";
		return 0;
	} catch (const std::exception& ex) {
		std::cerr << ex.what() << '\n';
		return 1;
	}
}

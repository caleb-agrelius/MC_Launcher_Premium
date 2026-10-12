#include "Game/PackInstance.h"
#include "Game/ServerSetup.h"
#include "Network/HttpClient.h"
#include "Storage/Settings.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Process.h"
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
	ServerInfo info;
	Check(SetupServer(http, request, info, {}, &error, nullptr), error);
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
	Check(WriteServerProperties(request.serverDir, props, &error), "Properties write failed");
	Check(ReadServerProperties(request.serverDir).port == 25570 && !ReadServerProperties(request.serverDir).onlineMode,
		"Properties did not round-trip");
	Check(Read(request.serverDir / "server.properties").find("custom-setting=yes") != std::string::npos,
		"Unknown server property was lost");
	Write(request.serverDir / "config/shared.txt", "user config");
	Write(request.serverDir / "world/level.dat", "world");
	index["versionId"] = "v2";
	Write(request.extractedDir / "modrinth.index.json", index.dump());
	Check(SetupServer(http, request, info, {}, &error, nullptr), error);
	Check(Read(request.serverDir / "config/shared.txt") == "user config" &&
		Read(request.serverDir / "world/level.dat") == "world" && EulaAccepted(request.serverDir) &&
		ReadServerProperties(request.serverDir).port == 25570, "Server update overwrote player data");
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
	Theme::Apply(0, 1.0f, 1.0f);
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
		CardChecks();
		ProcessChecks(dir, executable);
		std::cout << "Window settings, server setup, cards and console checks passed\n";
		return 0;
	} catch (const std::exception& ex) {
		std::cerr << ex.what() << '\n';
		return 1;
	}
}

#include "Services/Servers/ServerService.h"

#include "Network/Modrinth.h"
#include "Services/AppPaths.h"
#include "Storage/Installer.h"
#include "Util/PathSafety.h"
#include "Util/TracedFs.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace {

// Per-server memory, kept next to the server so it survives reinstalls of the launcher.
int LoadServerMemory(const fs::path& serverDir, int fallback)
{
	std::ifstream in(serverDir / ".mclp-memory");
	int memory = 0;
	return (in >> memory && memory >= 1024) ? memory : fallback;
}

void SaveServerMemory(const fs::path& serverDir, int memoryMb)
{
	std::error_code ec;
	TracedFs::CreateDirectories(serverDir, ec);
	TracedFs::OpenWrite(serverDir / ".mclp-memory", std::ios::trunc) << memoryMb << "\n";
}

fs::path ServerDir(const std::string& packId)
{
	return fs::path(AppPaths::kServersRoot) / packId;
}

}

ServerService::ServerService(const Runtime& runtime, SettingsStore& settings, PackLibrary& library)
	: m_rt(runtime), m_settings(settings), m_library(library), m_scope(runtime.tasks)
{
}

ServerState& ServerService::State(const std::string& packId)
{
	ServerState* entry;
	{
		// The setup worker looks servers up; inserting must not race with that.
		std::lock_guard<std::mutex> lock(m_mutex);
		entry = &m_servers[packId];
	}
	ServerState& server = *entry;
	if (!server.loaded) {
		const fs::path dir = ServerDir(packId);
		server.loaded = true;
		server.ready = LoadServerInfo(dir, server.info);
		server.props = ReadServerProperties(dir);
		server.eula = EulaAccepted(dir);
		server.memoryMb = LoadServerMemory(dir, std::max(4096, m_settings.Get().launch.maxMemoryMb));
		server.docker = ServerUsesDocker(dir);
	}
	return server;
}

int ServerService::RunningCount() const
{
	int running = 0;
	for (const auto& [packId, server] : m_servers) {
		if (server.running)
			++running;
	}
	return running;
}

bool ServerService::CanHost(const Modpack& pack) const
{
	return CanCreateServer(m_library.InstallDir(pack));
}

ServerSetupSnapshot ServerService::Setup(const std::string& packId)
{
	ServerSetupSnapshot snapshot;
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_servers.find(packId);
	if (it != m_servers.end()) {
		snapshot.busy = it->second.busy;
		snapshot.deleting = it->second.deleting;
		snapshot.step = it->second.step;
		snapshot.progress = it->second.progress;
		snapshot.error = it->second.error;
	}
	return snapshot;
}

bool ServerService::IsRunningOrBusy(const std::string& packId)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_servers.find(packId);
	return it != m_servers.end() && (it->second.running || it->second.busy);
}

void ServerService::ChooseVersion(const std::string& packId, bool has, const PackVersionInfo& version)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	ServerState& server = m_servers[packId];
	server.hasVersion = has;
	server.version = version;
}

bool ServerService::ChosenVersion(const std::string& packId, PackVersionInfo& version)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_servers.find(packId);
	if (it == m_servers.end() || !it->second.hasVersion)
		return false;
	version = it->second.version;
	return true;
}

void ServerService::CancelSetup(const std::string& packId)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_servers.find(packId);
	if (it != m_servers.end())
		it->second.cancel->store(true);
}

void ServerService::StartSetup(const Modpack& pack)
{
	auto cancel = std::make_shared<std::atomic<bool>>(false);
	// A version other than the installed one: setup downloads that version's .mrpack.
	PackFile otherVersion;
	fs::path otherInstalledDir; // another installed version's folder
	// The player's edits to the version the server is built from, and its game folder.
	ContentChanges serverChanges = m_library.ContentChangesOf(pack);
	fs::path changesGameDir = m_library.GameDir(pack);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		ServerState& state = m_servers[pack.id];
		if (state.hasVersion) {
			// Another installed version is used as it is; any other one is downloaded.
			Modpack wanted = pack;
			wanted.version = state.version.versionNumber;
			wanted.sourceVersionId = state.version.sourceVersionId;
			const Modpack* entry = m_library.Find(pack.id);
			const InstalledVersion* installed = entry ? FindInstalledVersion(*entry, VersionKey(wanted)) : nullptr;
			serverChanges.clear();
			if (installed) {
				otherInstalledDir = fs::absolute(fs::u8path(installed->installDir));
				serverChanges = installed->contentChanges;
				changesGameDir = fs::u8path(installed->gameDir);
			} else if (!state.version.files.empty())
				otherVersion = state.version.files.front();
			else {
				// Whole-pack sources have no single file to download; their servers are built
				// from an installed version, so don't quietly fall back to the active one.
				state.error = "Install " + state.version.versionNumber + " from Discover first. " +
					PackSourceName(pack.source) + " servers are built from an installed version.";
				return;
			}
		}
		state.busy = true;
		state.step = "Starting";
		state.progress = -1.0;
		state.error.clear();
		state.cancel = cancel;
	}
	ServerSetupRequest request;
	request.gameRoot = fs::absolute(AppPaths::kGameRoot);
	request.extractedDir = otherInstalledDir.empty() ? fs::absolute(m_library.InstallDir(pack)) : otherInstalledDir;
	request.serverDir = fs::absolute(ServerDir(pack.id));
	request.javaPath = m_settings.Get().launch.javaPath;
	request.removedFiles = RemovedContent(serverChanges);
	for (const auto& [path, change] : serverChanges) {
		if (change == "added" && path.rfind("mods/", 0) == 0)
			request.addedMods.push_back(fs::absolute(changesGameDir / fs::u8path(path)));
	}
	// Whole-pack installs: Modrinth knows which mods don't run on servers.
	request.findClientOnly = [this, cancel](const std::vector<std::string>& sha1s, std::set<std::string>& clientOnly) {
		return FindModrinthClientOnly(m_rt.http, sha1s, clientOnly, nullptr, cancel.get());
	};
	const std::string packId = pack.id;
	const std::string packName = pack.name;
	m_rt.log << "Setting up server for " << packId << "\n";
	m_scope.Run([this, request, packId, packName, cancel, otherVersion]() mutable {
		auto progress = [&](const std::string& step, double fraction) {
			std::lock_guard<std::mutex> lock(m_mutex);
			ServerState& state = m_servers.at(packId);
			state.step = step;
			state.progress = fraction;
		};
		ServerInfo info;
		std::string error;
		bool ok = true;
		try {
			if (!otherVersion.url.empty()) {
				// The chosen version's .mrpack, unpacked into servers/<id>/.mclp-pack/ (fresh each time).
				const fs::path zip = fs::absolute(fs::path(AppPaths::kDownloadsRoot) / packId / "server" / otherVersion.name);
				const fs::path unpacked = request.serverDir / ".mclp-pack";
				std::error_code ec;
				TracedFs::CreateDirectories(zip.parent_path(), ec);
				progress("Downloading " + otherVersion.name, -1.0);
				ok = m_rt.http.DownloadToFile(otherVersion.url, zip.string(), [&](double total, double now) {
					if (total > 0.0)
						progress("Downloading " + otherVersion.name, now / total);
				}, &error, cancel.get());
				if (ok) {
					progress("Unpacking " + otherVersion.name, -1.0);
					TracedFs::RemoveAll(unpacked, ec);
					ok = InstallZip(zip.string(), unpacked.string(), nullptr, &error, cancel.get());
				}
				request.extractedDir = unpacked;
			}
			if (ok)
				ok = SetupServer(m_rt.http, request, info, progress, &error, cancel.get());
		} catch (const std::exception& ex) {
			error = std::string("Server setup failed: ") + ex.what();
		}
		const bool cancelled = cancel->load();
		m_rt.ui.Post([this, packId, packName, ok, cancelled, info, error]() {
			std::lock_guard<std::mutex> lock(m_mutex);
			ServerState& state = m_servers.at(packId);
			state.busy = false;
			state.error = (ok || cancelled) ? std::string() : error;
			if (ok) {
				state.ready = true;
				state.info = info;
				state.props = ReadServerProperties(ServerDir(packId));
				state.eula = EulaAccepted(ServerDir(packId));
				state.modsLoaded = false;
				m_rt.log << "Server for " << packId << " ready: " << info.loader << " " << info.minecraftVersion << "\n";
				m_rt.notify.Notify(NoticeKind::Success, "Server files ready", packName + " can now be started.");
			} else if (!cancelled) {
				m_rt.log << "Server setup for " << packId << " failed: " << error << "\n";
				m_rt.notify.Notify(NoticeKind::Error, "Server setup failed", packName + ": " + error);
			}
		});
	});
}

void ServerService::StartDelete(const std::string& packId, const std::string& packName)
{
	if (!IsPlainFolderName(packId))
		return;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		ServerState& state = m_servers[packId];
		state.busy = true;
		state.deleting = true;
		state.step = "Deleting server files";
		state.progress = -1.0;
		state.error.clear();
	}
	m_rt.log << "Deleting server " << packId << "\n";
	m_scope.Run([this, packId, packName]() {
		std::string error;
		const bool ok = RemoveServer(ServerDir(packId), &error);
		m_rt.ui.Post([this, packId, packName, ok, error]() {
			std::lock_guard<std::mutex> lock(m_mutex);
			ServerState& state = m_servers.at(packId);
			if (ok) {
				const unsigned generation = state.generation + 1;
				state = ServerState{}; // re-read from disk (now empty) next time it's shown
				state.generation = generation;
				m_orphans.erase(packId);
				m_rt.notify.Notify(NoticeKind::Success, "Server deleted", packName + "'s server files were removed.");
			} else {
				state.busy = false;
				state.deleting = false;
				state.error = error;
				m_rt.log << "Delete server " << packId << " failed: " << error << "\n";
				m_rt.notify.Notify(NoticeKind::Error, "Couldn't delete the server", error);
			}
		});
	});
}

bool ServerService::StartProcess(const std::string& packId)
{
	ServerState& server = State(packId);
	const fs::path serverDir = ServerDir(packId);
	server.process = std::make_unique<ServerProcess>();
	std::string error;
	const std::vector<std::string> command = ServerCommand(server.info, server.memoryMb);
	if (server.process->Start(command, fs::absolute(serverDir), fs::absolute(serverDir / "logs" / "launcher-console.log"), &error)) {
		server.running = true;
		m_rt.log << "Started server " << packId << "\n";
		return true;
	}
	m_rt.notify.Notify(NoticeKind::Error, "Couldn't start the server", error);
	server.process.reset();
	return false;
}

void ServerService::RefreshOrphans()
{
	m_orphans.clear();
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(AppPaths::kServersRoot, ec)) {
		const std::string id = entry.path().filename().string();
		if (!entry.is_directory(ec) || m_library.Contains(id) || !IsPlainFolderName(id))
			continue;
		Modpack pack;
		pack.id = id;
		pack.name = id;
		m_orphans[id] = pack;
	}
}

void ServerService::SyncProcesses()
{
	for (auto& [packId, server] : m_servers) {
		const bool running = server.process && server.process->Running();
		if (server.running && !running) {
			const int code = server.process ? server.process->ExitCode() : 0;
			const Modpack* pack = m_library.Find(packId);
			m_rt.notify.Notify(code == 0 ? NoticeKind::Info : NoticeKind::Error, "Server stopped",
				code == 0 ? (pack ? pack->name : packId)
					: "It exited with code " + std::to_string(code) + ". See the console for details.");
			server.stopRequestedAt = -1.0;
		}
		server.running = running;
	}
}

void ServerService::AcceptEula(const std::string& packId, ServerState& state)
{
	std::string error;
	if (::AcceptEula(ServerDir(packId), &error))
		state.eula = true;
	else
		m_rt.log << error << "\n";
}

void ServerService::SaveProperties(const std::string& packId, const ServerState& state)
{
	std::string error;
	if (!WriteServerProperties(ServerDir(packId), state.props, &error))
		m_rt.log << error << "\n";
}

void ServerService::SaveMemory(const std::string& packId, const ServerState& state)
{
	SaveServerMemory(ServerDir(packId), state.memoryMb);
}

bool ServerService::SetModEnabled(const std::string& packId, ServerState& state, const std::string& fileName, bool enabled,
	std::string* error)
{
	if (!SetServerModEnabled(ServerDir(packId), fileName, enabled, error))
		return false;
	for (ServerMod& mod : state.mods) {
		if (mod.fileName == fileName)
			mod.enabled = enabled;
	}
	m_rt.log << "Server " << packId << ": " << (enabled ? "enabled " : "disabled ") << fileName << "\n";
	return true;
}

bool ServerService::SetUsesDocker(const std::string& packId, ServerState& state, bool docker, std::string* error)
{
	if (!SetServerUsesDocker(ServerDir(packId), docker, error))
		return false;
	state.docker = docker;
	m_rt.log << "Server " << packId << (docker ? ": runs in Docker" : ": runs with Java") << "\n";
	return true;
}

void ServerService::WriteComposeFile(const std::string& packId, ServerState& state)
{
	const fs::path dir = ServerDir(packId);
	std::string error;
	state.dockerFailed = !WriteDockerCompose(dir, packId, state.info, state.memoryMb, &error);
	state.dockerMessage = state.dockerFailed ? error : "Wrote " + (dir / "docker-compose.yml").string();
}

void ServerService::ReloadMods(const std::string& packId, ServerState& state)
{
	state.mods = ListServerMods(ServerDir(packId));
	state.modsLoaded = true;
}

void ServerService::CheckDocker()
{
	if (m_docker == DockerStatus::Checking)
		return;
	m_docker = DockerStatus::Checking;
	m_scope.Run([this]() {
		std::error_code ec;
		const fs::path logFile = fs::absolute(fs::path(AppPaths::kServersRoot) / "docker-check.log", ec);
		TracedFs::CreateDirectories(logFile.parent_path(), ec);
		int exitCode = -1;
		std::string error;
		const bool started = RunProcess({ "docker", "version", "--format", "{{.Server.Version}}" }, fs::current_path(ec),
			logFile, true, &exitCode, &error, &m_rt.shuttingDown);
		const DockerStatus state = !started ? DockerStatus::Missing : exitCode == 0 ? DockerStatus::Ready : DockerStatus::Stopped;
		m_rt.ui.Post([this, state]() { m_docker = state; });
	});
}

void ServerService::StartDockerCompose(const std::string& packId, const std::vector<std::string>& args, const std::string& doneMessage)
{
	ServerState& state = m_servers[packId];
	const fs::path dir = fs::absolute(ServerDir(packId));
	std::string error;
	if (!WriteDockerCompose(dir, packId, state.info, state.memoryMb, &error)) {
		state.dockerFailed = true;
		state.dockerMessage = error;
		return;
	}
	state.dockerBusy = true;
	state.dockerFailed = false;
	std::vector<std::string> command = { "docker" };
	command.insert(command.end(), args.begin(), args.end());
	state.dockerMessage = "Running";
	for (const std::string& arg : command)
		state.dockerMessage += " " + arg;
	state.dockerMessage += "...";
	m_rt.log << "Docker for " << packId << ": " << state.dockerMessage << "\n";
	m_scope.Run([this, packId, dir, command, doneMessage]() {
		int exitCode = -1;
		std::string error;
		const fs::path logFile = dir / "logs" / "docker.log";
		std::error_code ec;
		TracedFs::CreateDirectories(logFile.parent_path(), ec);
		const bool ok = RunProcess(command, dir, logFile, true, &exitCode, &error, &m_rt.shuttingDown, false) && exitCode == 0;
		if (error.empty() && !ok)
			error = "Docker exited with code " + std::to_string(exitCode) + ". See " + logFile.string();
		const bool up = command.size() > 2 && command[2] == "up";
		m_rt.ui.Post([this, packId, ok, up, error, doneMessage]() {
			ServerState& state = m_servers[packId];
			state.dockerBusy = false;
			state.dockerFailed = !ok;
			if (ok)
				state.dockerUp = up;
			state.dockerMessage = ok ? doneMessage : error;
			m_rt.log << "Docker for " << packId << (ok ? ": done" : " failed: " + error) << "\n";
		});
	});
}

void ServerService::StopAll()
{
	for (auto& [packId, server] : m_servers) {
		if (server.process && server.process->Running()) {
			m_rt.log << "Stopping server " << packId << "\n";
			server.process->Send("stop");
		}
	}
	for (auto& [packId, server] : m_servers) {
		if (server.process && !server.process->WaitForExit(20000))
			server.process->Kill();
		std::lock_guard<std::mutex> lock(m_mutex);
		server.cancel->store(true);
	}
}

#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Model/Modpack.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Servers/ServerState.h"
#include "Services/Settings/SettingsStore.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

// Dedicated servers for library packs: building the server files, running the server process
// (or its Docker container), configuring it and deleting it. The per-pack states live here; the
// Servers page draws them. UI thread only except the setup fields (see ServerState).
class ServerService {
public:
	ServerService(const Runtime& runtime, SettingsStore& settings, PackLibrary& library);

	// The state for a pack's server, read from disk the first time it is asked for.
	ServerState& State(const std::string& packId);
	// Packs with servers that are running (the Servers count in the sidebar).
	int RunningCount() const;

	// Extracted .mrpacks and whole-pack installs can both be hosted (see ServerSetup.h).
	// Checks the pack's active version's install folder.
	bool CanHost(const Modpack& pack) const;

	// Setup/delete progress, copied under the lock.
	ServerSetupSnapshot Setup(const std::string& packId);
	// True while the server runs or is being set up or deleted.
	bool IsRunningOrBusy(const std::string& packId);
	// Which pack version Create/Update server files should use (the installed one when !has).
	void ChooseVersion(const std::string& packId, bool has, const PackVersionInfo& version);
	bool ChosenVersion(const std::string& packId, PackVersionInfo& version);

	// Builds (or updates) the server's files on a worker.
	void StartSetup(const Modpack& pack);
	void CancelSetup(const std::string& packId);
	// Deletes servers/<pack id>/ on a worker. The server must be stopped and not setting up.
	void StartDelete(const std::string& packId, const std::string& packName);

	// Starts the server's Java process. False if it couldn't start (the player is told why).
	bool StartProcess(const std::string& packId);

	// Servers whose pack is no longer in My Library (uninstalled without deleting the server),
	// so they can still be run or deleted. Named after their folder.
	const std::map<std::string, Modpack>& Orphans() const { return m_orphans; }
	void RefreshOrphans();

	// Keeps the states in step with their processes: notices servers that stopped.
	void SyncProcesses();

	// Settings the player edits on the page; each saves to the server's folder.
	void AcceptEula(const std::string& packId, ServerState& state);
	void SaveProperties(const std::string& packId, const ServerState& state);
	void SaveMemory(const std::string& packId, const ServerState& state);
	bool SetModEnabled(const std::string& packId, ServerState& state, const std::string& fileName, bool enabled, std::string* error);
	bool SetUsesDocker(const std::string& packId, ServerState& state, bool docker, std::string* error);
	void WriteComposeFile(const std::string& packId, ServerState& state);
	void ReloadMods(const std::string& packId, ServerState& state);

	// Docker is checked on a worker each time the Servers page opens: "docker version" fails when
	// Docker is missing or its engine isn't running.
	DockerStatus Docker() const { return m_docker; }
	void CheckDocker();
	// Writes the server's docker-compose.yml, then runs "docker compose <args>" in its folder.
	void StartDockerCompose(const std::string& packId, const std::vector<std::string>& args, const std::string& doneMessage);

	// At shutdown: running servers get "stop" (they save their worlds), then a few seconds
	// before being killed.
	void StopAll();

private:
	Runtime m_rt;
	SettingsStore& m_settings;
	PackLibrary& m_library;
	std::mutex m_mutex;                          // guards the setup fields of the states, and inserting into m_servers
	std::map<std::string, ServerState> m_servers; // keyed by pack id; entries only added on the UI thread
	std::map<std::string, Modpack> m_orphans;
	DockerStatus m_docker = DockerStatus::Unknown;
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};

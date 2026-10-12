#pragma once

#include "Game/ServerSetup.h"
#include "Network/PackSources.h"
#include "Util/Process.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A pack's dedicated server (Servers page). UI thread only, except the setup fields,
// which the setup worker updates under ServerService's mutex.
struct ServerState {
	bool loaded = false; // the fields below have been read from disk
	bool ready = false;  // server files are installed (.mclp-server.json exists)
	ServerInfo info;
	ServerProperties props;
	bool eula = false;
	int memoryMb = 4096;
	std::unique_ptr<ServerProcess> process;
	bool running = false;        // as of last frame, to notice when the server stops
	double stopRequestedAt = -1.0;
	bool docker = false;       // runs with docker-minecraft-server instead of Java (ServerUsesDocker)
	bool dockerUp = false;     // the last "docker compose up" from this launcher succeeded, and no "down" since
	std::vector<ServerMod> mods; // servers/<id>/mods, for turning mods on and off
	bool modsLoaded = false;
	bool dockerBusy = false;   // a "docker compose" command is running
	std::string dockerMessage; // the last Docker action's result
	bool dockerFailed = false;
	// Setup, written by the worker under the service's mutex.
	bool busy = false;
	bool deleting = false; // busy deleting the server folder, which can't be cancelled
	std::string step;
	double progress = -1.0;
	std::string error;
	std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
	// The pack version Create/Update server files uses; unset = the version installed in My Library.
	bool hasVersion = false;
	PackVersionInfo version;
	// Bumped when the server is deleted and this state starts over, so a view of it can start over too.
	unsigned generation = 0;
};

// The setup fields of a ServerState, copied under the lock.
struct ServerSetupSnapshot {
	bool busy = false;
	bool deleting = false;
	std::string step;
	double progress = -1.0;
	std::string error;
};

// Docker (docker-minecraft-server), checked each time the Servers page opens.
enum class DockerStatus { Unknown, Checking, Missing, Stopped, Ready };

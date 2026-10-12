#pragma once

#include "Model/Modpack.h"
#include "UI/Dialogs/Dialogs.h"
#include "UI/Pages/Servers/DockerStatusView.h"
#include "UI/Pages/Servers/ServerConsole.h"
#include "UI/Pages/Servers/ServerDockerTab.h"
#include "UI/Pages/Servers/ServerFilesCard.h"
#include "UI/Pages/Servers/ServerModsTab.h"
#include "UI/Pages/Servers/ServerRunCard.h"
#include "UI/Pages/Servers/ServerSettingsTab.h"
#include "UI/Pages/Servers/ServerView.h"

#include <string>

class ImageCache;
class PackLibrary;
class ServerService;

// The Servers page's right pane for the selected pack: its title and status, then the Run,
// Settings, Mods and Docker tabs.
class ServerDetails {
public:
	ServerDetails(ServerService& servers, PackLibrary& library, ImageCache& images, IDialogs& dialogs,
		ServerFilesCard& files, ServerRunCard& run, ServerConsole& console, ServerSettingsTab& settings,
		ServerModsTab& mods, ServerDockerTab& docker)
		: m_servers(servers), m_library(library), m_images(images), m_dialogs(dialogs), m_files(files), m_run(run),
		  m_console(console), m_settings(settings), m_mods(mods), m_docker(docker) {}

	void Draw(const std::string& packId, const Modpack& pack, ServerView& view, const HostNetwork& network);

private:
	void DrawHeader(const ServerTabContext& ctx);
	// The pack's files are gone and no finished server exists: say so, and offer to clear the folder.
	void DrawUnavailable(const ServerTabContext& ctx);

	ServerService& m_servers;
	PackLibrary& m_library;
	ImageCache& m_images;
	IDialogs& m_dialogs;
	ServerFilesCard& m_files;
	ServerRunCard& m_run;
	ServerConsole& m_console;
	ServerSettingsTab& m_settings;
	ServerModsTab& m_mods;
	ServerDockerTab& m_docker;
};

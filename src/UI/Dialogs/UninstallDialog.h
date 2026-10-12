#pragma once

#include <string>

class DownloadService;
class PackLibrary;
class PlayService;
class ServerService;
class SettingsStore;
class UninstallService;
class UpdateService;

// "Uninstall pack?": one version of a pack, or all of it, with the option to take its server along.
class UninstallDialog {
public:
	UninstallDialog(PackLibrary& library, UninstallService& uninstaller, ServerService& servers, PlayService& plays,
		DownloadService& downloads, UpdateService& updates, SettingsStore& settings)
		: m_library(library), m_uninstaller(uninstaller), m_servers(servers), m_plays(plays), m_downloads(downloads),
		  m_updates(updates), m_settings(settings) {}

	// With an empty versionKey the whole pack goes.
	void Open(const std::string& packId, const std::string& versionKey);
	void Draw();

private:
	PackLibrary& m_library;
	UninstallService& m_uninstaller;
	ServerService& m_servers;
	PlayService& m_plays;
	DownloadService& m_downloads;
	UpdateService& m_updates;
	SettingsStore& m_settings;
	std::string m_packId; // empty = closed
	std::string m_versionKey;
	bool m_deleteServer = false;
};

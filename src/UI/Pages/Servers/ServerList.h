#pragma once

#include "Model/Modpack.h"

#include <map>
#include <string>

class ImageCache;
class PackLibrary;
class ServerService;
class SettingsStore;

// The Servers page's left pane: every pack that has (or could have) a server, with its status,
// then the splitter that resizes it.
class ServerList {
public:
	ServerList(ServerService& servers, PackLibrary& library, SettingsStore& settings, ImageCache& images)
		: m_servers(servers), m_library(library), m_settings(settings), m_images(images) {}

	// Draws the pane and its splitter, leaving the cursor so the details can follow on the same
	// line. Picking a row changes selected.
	void Draw(const std::map<std::string, const Modpack*>& packs, std::string& selected);

private:
	ServerService& m_servers;
	PackLibrary& m_library;
	SettingsStore& m_settings;
	ImageCache& m_images;
};

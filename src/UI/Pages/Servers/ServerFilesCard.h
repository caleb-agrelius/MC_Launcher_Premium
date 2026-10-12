#pragma once

#include "UI/Dialogs/Dialogs.h"
#include "UI/Pages/Servers/ServerView.h"

class PackLibrary;
class ServerService;
class VersionPicker;

// The Run tab's first card: create or update the server's files, pick which pack version they
// come from, open or delete the folder.
class ServerFilesCard {
public:
	ServerFilesCard(ServerService& servers, PackLibrary& library, VersionPicker& versions, IDialogs& dialogs)
		: m_servers(servers), m_library(library), m_versions(versions), m_dialogs(dialogs) {}

	void Draw(const ServerTabContext& ctx);

private:
	void DrawVersionPicker(const ServerTabContext& ctx);

	ServerService& m_servers;
	PackLibrary& m_library;
	VersionPicker& m_versions;
	IDialogs& m_dialogs;
};

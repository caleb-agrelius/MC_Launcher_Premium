#pragma once

#include "UI/Pages/Servers/ServerView.h"

class ServerService;

// The Mods tab: turn single mods off, e.g. one that crashes the server.
class ServerModsTab {
public:
	explicit ServerModsTab(ServerService& servers) : m_servers(servers) {}

	void Draw(const ServerTabContext& ctx);

private:
	ServerService& m_servers;
};

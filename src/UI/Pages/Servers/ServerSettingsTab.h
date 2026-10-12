#pragma once

#include "UI/Pages/Servers/ServerView.h"

class ServerService;

// The Settings tab: server.properties fields (MOTD, port, Host IP, game rules...) and memory.
class ServerSettingsTab {
public:
	explicit ServerSettingsTab(ServerService& servers) : m_servers(servers) {}

	void Draw(const ServerTabContext& ctx);

private:
	// The Host IP dropdown and, for a custom address, its text field. True when the address changed.
	bool DrawHostIp(const ServerTabContext& ctx, float labelWidth);

	ServerService& m_servers;
};

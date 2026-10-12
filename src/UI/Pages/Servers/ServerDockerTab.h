#pragma once

#include "UI/Pages/Servers/ServerView.h"

class ServerService;

// The Docker tab: switch this server between Java and docker-minecraft-server.
class ServerDockerTab {
public:
	explicit ServerDockerTab(ServerService& servers) : m_servers(servers) {}

	void Draw(const ServerTabContext& ctx);

private:
	ServerService& m_servers;
};

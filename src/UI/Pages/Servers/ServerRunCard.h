#pragma once

#include "UI/Pages/Servers/ServerView.h"

class ServerService;

// The Run tab's second card: the EULA, Start/Stop (Java or Docker), and where players join.
class ServerRunCard {
public:
	explicit ServerRunCard(ServerService& servers) : m_servers(servers) {}

	void Draw(const ServerTabContext& ctx);

private:
	void DrawButtons(const ServerTabContext& ctx);
	void DrawJoinAddresses(const ServerTabContext& ctx);

	ServerService& m_servers;
};

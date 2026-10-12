#pragma once

#include "UI/Navigation.h"
#include "UI/Pages/IPage.h"
#include "UI/Pages/Servers/ServerDetails.h"
#include "UI/Pages/Servers/ServerList.h"
#include "UI/Pages/Servers/ServerView.h"

#include <map>
#include <string>

class PackLibrary;
class ServerService;

// Servers: host a library pack for friends. A list of packs on the left, the selected pack's
// server on the right.
class ServersPage final : public IPackPage {
public:
	ServersPage(ServerService& servers, PackLibrary& library, ServerList& list, ServerDetails& details,
		INavigator& navigator)
		: m_servers(servers), m_library(library), m_list(list), m_details(details), m_navigator(navigator) {}

	// A library card's Server button: open the page with this pack selected.
	void SelectPack(const std::string& packId, bool) override { m_selected = packId; }

	void DrawHeader() override;
	void Draw(const PageFrame& frame) override;
	void OnShown() override;

private:
	ServerView& ViewOf(const std::string& packId, const ServerState& server);

	ServerService& m_servers;
	PackLibrary& m_library;
	ServerList& m_list;
	ServerDetails& m_details;
	INavigator& m_navigator;
	std::string m_selected;
	HostNetwork m_network;
	std::map<std::string, ServerView> m_views;
};

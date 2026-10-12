#include "UI/Pages/Servers/ServersPage.h"

#include "Platform/Platform.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Servers/ServerService.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Widgets.h"

void ServersPage::DrawHeader()
{
	UiHelpers::DrawPageHeader("MINECRAFT  /  SERVERS", "Servers", "Host a modpack for your friends in one click.");
}

void ServersPage::OnShown()
{
	m_servers.RefreshOrphans();
	m_network.lanAddress = Platform::LocalNetworkAddress();
	m_network.adapters = Platform::LocalNetworkAddresses();
	m_servers.CheckDocker();
}

ServerView& ServersPage::ViewOf(const std::string& packId, const ServerState& server)
{
	ServerView& view = m_views[packId];
	if (view.generation != server.generation) {
		view = ServerView{}; // the server was deleted; its screen starts over
		view.generation = server.generation;
	}
	return view;
}

void ServersPage::Draw(const PageFrame&)
{
	m_servers.SyncProcesses();

	// Library packs, plus servers whose pack was uninstalled.
	std::map<std::string, const Modpack*> packs;
	for (const auto& [packId, pack] : m_library.Packs())
		packs[packId] = &pack;
	for (const auto& [packId, pack] : m_servers.Orphans()) {
		if (!packs.count(packId))
			packs[packId] = &pack;
	}
	if (packs.empty()) {
		if (Widgets::EmptyState("No servers yet",
				"Install a modpack first. Then you can turn it into a server for your friends here.", "Browse modpacks"))
			m_navigator.ShowPage(Page::Discover);
		return;
	}
	if (!packs.count(m_selected))
		m_selected = packs.begin()->first;

	m_list.Draw(packs, m_selected);
	m_details.Draw(m_selected, *packs.at(m_selected), ViewOf(m_selected, m_servers.State(m_selected)), m_network);
}

#pragma once

#include "UI/Navigation.h"
#include "UI/Shell/AccountPanel.h"
#include "UI/Shell/Mascot.h"

class DownloadService;
class ServerService;

// The left column: the mascot and name, the page links (with counts of active downloads and
// running servers), and the account.
class Sidebar {
public:
	Sidebar(Mascot& mascot, AccountPanel& account, DownloadService& downloads, ServerService& servers,
		INavigator& navigator)
		: m_mascot(mascot), m_account(account), m_downloads(downloads), m_servers(servers), m_navigator(navigator) {}

	// current is the page on screen.
	void Draw(Page current);

private:
	Mascot& m_mascot;
	AccountPanel& m_account;
	DownloadService& m_downloads;
	ServerService& m_servers;
	INavigator& m_navigator;
};

#pragma once

class AccountService;
class SettingsStore;

// The sidebar's ACCOUNT section (who Minecraft is launched as) and the "Play offline" name prompt.
class AccountPanel {
public:
	AccountPanel(AccountService& accounts, SettingsStore& settings) : m_accounts(accounts), m_settings(settings) {}

	// The section, inside the sidebar.
	void Draw();
	// The offline-name popup. Draw it outside the sidebar's child window.
	void DrawOfflinePopup();

private:
	AccountService& m_accounts;
	SettingsStore& m_settings;
	bool m_openOfflinePopup = false;
	char m_offlineName[17] = "";
};

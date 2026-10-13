#pragma once

class AccountService;
class SettingsStore;

// The sidebar's ACCOUNT section: who Minecraft is launched as. Only Microsoft accounts can be added.
class AccountPanel {
public:
	AccountPanel(AccountService& accounts, SettingsStore& settings) : m_accounts(accounts), m_settings(settings) {}

	// The section, inside the sidebar.
	void Draw();

private:
	AccountService& m_accounts;
	SettingsStore& m_settings;
};

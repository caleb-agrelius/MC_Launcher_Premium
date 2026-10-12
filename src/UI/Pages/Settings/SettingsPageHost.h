#pragma once

#include "Core/WindowControl.h"
#include "UI/Pages/IPage.h"
#include "UI/Pages/Settings/SettingsContext.h"
#include "UI/Pages/Settings/SettingsPage.h"

class AccountService;
class DiscoverService;
class JavaScanService;
class PlayService;
class SettingsStore;
class SharedFilesService;

// Connects the Settings page to the app: it describes the machine, the account and the
// background jobs, and reacts when the player changes something (saving, the CurseForge key,
// shared mod files).
class SettingsPageHost final : public IPage {
public:
	SettingsPageHost(SettingsStore& settings, AccountService& accounts, JavaScanService& javas,
		SharedFilesService& sharedFiles, PlayService& plays, DiscoverService& discover, IDisplayMetrics& display);

	// The facts the Settings page (and the per-pack launch settings dialog) show.
	SettingsPageContext Context(double shownAt);

	// One pack's launch options, for the dialog opened from Manage. True when settings changed.
	bool DrawPackLaunch(Settings& settings, const std::string& packId);

	void DrawHeader() override;
	void Draw(const PageFrame& frame) override;

private:

	SettingsStore& m_settings;
	AccountService& m_accounts;
	JavaScanService& m_javas;
	SharedFilesService& m_sharedFiles;
	PlayService& m_plays;
	DiscoverService& m_discover;
	IDisplayMetrics& m_display;
	SettingsPage m_page;
};

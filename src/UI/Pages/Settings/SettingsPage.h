#pragma once

#include "UI/Pages/Settings/AboutTab.h"
#include "UI/Pages/Settings/AccountTab.h"
#include "UI/Pages/Settings/AppearanceTab.h"
#include "UI/Pages/Settings/DownloadsTab.h"
#include "UI/Pages/Settings/GameOptionsTab.h"
#include "UI/Pages/Settings/MinecraftTab.h"
#include "UI/Pages/Settings/PackLaunchSettings.h"
#include "UI/Pages/Settings/SettingsContext.h"
#include "UI/Pages/Settings/SettingsLayout.h"

#include <string>

struct Settings;

// The Settings page: a tab strip over the tabs, each drawing its own cards.
class SettingsPage {
public:
	// Draws the page into the current window. Edits settings in place and returns true if
	// anything changed (the caller saves and applies theme/scale changes).
	bool Draw(Settings& settings, const SettingsPageContext& context);

	// One pack's launch options, for the dialog opened from My Library.
	bool DrawPackLaunch(Settings& settings, const std::string& packId, const SettingsPageContext& context);

private:
	enum class Tab { Appearance, Minecraft, GameOptions, Account, Downloads, About };

	void DrawTabStrip();
	ISettingsTab& TabFor(Tab tab);

	Tab m_tab = Tab::Appearance;
	SettingsLayout m_layout;
	AppearanceTab m_appearance;
	MinecraftTab m_minecraft;
	GameOptionsTab m_gameOptions;
	AccountTab m_account;
	DownloadsTab m_downloads;
	AboutTab m_about;
	PackLaunchSettings m_packLaunch;
};

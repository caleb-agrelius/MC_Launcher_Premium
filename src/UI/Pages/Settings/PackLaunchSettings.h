#pragma once

#include "UI/Pages/Settings/LaunchSettingsCards.h"

#include <string>

struct Settings;

// The per-pack launch options (memory, Java, JVM arguments, window) shown from My Library:
// a toggle to override Settings > Minecraft, then the same cards. True when settings changed
// and should be saved.
class PackLaunchSettings {
public:
	bool Draw(Settings& settings, const std::string& packId, const SettingsPageContext& context, SettingsLayout& ui);

private:
	LaunchSettingsCards m_cards;
};

#pragma once

#include "UI/Pages/Settings/LaunchSettingsCards.h"
#include "UI/Pages/Settings/SettingsTab.h"

// Settings > Minecraft: the launch defaults every pack uses.
class MinecraftTab final : public ISettingsTab {
public:
	bool Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui) override;

private:
	LaunchSettingsCards m_cards;
};

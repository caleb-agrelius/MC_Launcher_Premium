#pragma once

#include "UI/Pages/Settings/SettingsTab.h"

// Settings > About: version, machine facts, shortcuts and credits.
class AboutTab final : public ISettingsTab {
public:
	bool Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui) override;
};

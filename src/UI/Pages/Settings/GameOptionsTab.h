#pragma once

#include "UI/Pages/Settings/SettingsTab.h"

// Settings > Game options: vanilla options.txt values applied to every pack.
class GameOptionsTab final : public ISettingsTab {
public:
	bool Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui) override;
};

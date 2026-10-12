#pragma once

#include "UI/Pages/Settings/SettingsTab.h"

// Settings > Account: who is signed in, and the Microsoft client ID.
class AccountTab final : public ISettingsTab {
public:
	bool Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui) override;
};

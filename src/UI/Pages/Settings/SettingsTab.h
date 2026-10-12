#pragma once

#include "UI/Pages/Settings/SettingsContext.h"
#include "UI/Pages/Settings/SettingsLayout.h"

struct Settings;

// One tab of the Settings page. Draws its cards and edits settings in place; true when
// something changed (the caller saves and applies it).
class ISettingsTab {
public:
	virtual ~ISettingsTab() = default;
	virtual bool Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui) = 0;
};

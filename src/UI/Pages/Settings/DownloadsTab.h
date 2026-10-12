#pragma once

#include "UI/Pages/Settings/SettingsTab.h"

// Settings > Downloads: search size, the CurseForge key, shared mod files and folders.
class DownloadsTab final : public ISettingsTab {
public:
	bool Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui) override;
};

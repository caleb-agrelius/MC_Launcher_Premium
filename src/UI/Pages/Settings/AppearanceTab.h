#pragma once

#include "UI/Pages/Settings/SettingsTab.h"
#include "UI/Pages/Settings/ThemeEditor.h"

// Settings > Appearance: theme, the theme editor, UI scale and motion.
class AppearanceTab final : public ISettingsTab {
public:
	bool Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui) override;

private:
	ThemeEditor m_themeEditor;
};

#pragma once

#include "UI/Pages/Settings/SettingsLayout.h"

struct CustomTheme;
struct Settings;
namespace Theme { struct ThemeInfo; }

// Editor for the selected custom theme. Edits apply live: ThemeController rebuilds the style
// when the custom themes change.
class ThemeEditor {
public:
	// True when the theme (or the list of themes) changed.
	bool Draw(Settings& settings, SettingsLayout& ui);
	// A new custom theme, copied from base, with an unused id and name.
	static CustomTheme NewCustomTheme(const Settings& settings, const Theme::ThemeInfo& base);
};

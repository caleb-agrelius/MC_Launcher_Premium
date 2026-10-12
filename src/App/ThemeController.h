#pragma once

#include "Core/WindowControl.h"

#include <cstddef>

class GlfwWindow;
class SettingsStore;

// Keeps the look in step with the settings and the monitor: loads the fonts and first theme at
// startup, then each frame rebuilds the style when the theme, the UI scale or the monitor's DPI
// changed. The UI scale it applies is the monitor's DPI scale times the player's setting.
class ThemeController final : public IDisplayMetrics {
public:
	ThemeController(GlfwWindow& window, SettingsStore& settings) : m_window(window), m_settings(settings) {}

	// Call once the ImGui context exists, before its backends start.
	void Initialize();
	// Call at the start of each frame, before NewFrame().
	void Update();

	// The DPI scale the style was last built for.
	float DpiScale() const override { return m_dpiScale; }

private:
	void ApplyAll(bool recolorTitleBar);

	GlfwWindow& m_window;
	SettingsStore& m_settings;
	float m_dpiScale = 1.0f;
	float m_appliedUiScale = 1.0f;
	std::size_t m_appliedTheme = 0;
	int m_appliedThemeRevision = 0;
};

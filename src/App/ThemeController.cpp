#include "App/ThemeController.h"

#include "App/GlfwWindow.h"
#include "Services/Settings/SettingsStore.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

void ThemeController::ApplyAll(bool recolorTitleBar)
{
	Theme::Apply(m_appliedTheme, m_dpiScale, m_appliedUiScale);
	if (recolorTitleBar)
		m_window.ApplyTitleBar(Theme::C().light == false, Theme::C().windowBg, Theme::C().text);
	m_window.SetMinimumSize(static_cast<int>(Theme::S(900.0f)), static_cast<int>(Theme::S(600.0f)));
}

void ThemeController::Initialize()
{
	const Settings& settings = m_settings.Get();
	m_dpiScale = m_window.ContentScale();
	m_appliedUiScale = settings.uiScale;
	Theme::SetCustomThemes(settings.customThemes);
	m_appliedTheme = Theme::FindTheme(settings.theme);
	m_appliedThemeRevision = Theme::Revision();
	Theme::LoadFonts();
	ApplyAll(true);
	Widgets::SetReduceMotion(settings.reduceMotion);
}

void ThemeController::Update()
{
	const Settings& settings = m_settings.Get();
	const float contentScale = m_window.ContentScale();
	Theme::SetCustomThemes(settings.customThemes); // picks up edits from the theme editor
	const std::size_t wantedTheme = Theme::FindTheme(settings.theme);
	const bool themeChanged = wantedTheme != m_appliedTheme || Theme::Revision() != m_appliedThemeRevision;
	if (contentScale > 0.0f && (contentScale != m_dpiScale || settings.uiScale != m_appliedUiScale || themeChanged)) {
		m_appliedThemeRevision = Theme::Revision();
		m_dpiScale = contentScale;
		m_appliedUiScale = settings.uiScale;
		m_appliedTheme = wantedTheme;
		ApplyAll(themeChanged);
	}
	Widgets::SetReduceMotion(settings.reduceMotion);
}

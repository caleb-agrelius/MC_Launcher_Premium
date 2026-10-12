#include "App/ThemeController.h"

#include "App/GlfwWindow.h"
#include "Services/Settings/SettingsStore.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

namespace {
constexpr float kMinWindowWidth = 1180.0f;
constexpr float kMinWindowHeight = 720.0f;
}

void ThemeController::ApplyAll(bool recolorTitleBar)
{
	Theme::Apply(m_appliedTheme, m_dpiScale, m_appliedUiScale);
	if (recolorTitleBar)
		m_window.ApplyTitleBar(Theme::C().light == false, Theme::C().windowBg, Theme::C().text);
	// The smallest window where every page's layout still fits (Discover's filter row and the sidebar are the
	// widest and tallest), in logical pixels so it grows with Windows scaling and the UI scale.
	m_window.SetMinimumSize(static_cast<int>(Theme::S(kMinWindowWidth)), static_cast<int>(Theme::S(kMinWindowHeight)));
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
	Widgets::SetAnimationSpeed(settings.animationSpeed);
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
	Widgets::SetAnimationSpeed(settings.animationSpeed);
}

#include "UI/Shell/Shortcuts.h"

#include "Services/Settings/SettingsStore.h"
#include "UI/Pages/Discover/DiscoverPage.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kUiScaleStep = 0.1f;

// Rounds to the nearest 5% and keeps the scale within the supported range.
float NormalizeUiScale(float scale)
{
	return std::clamp(std::round(scale * 20.0f) / 20.0f, kMinUiScale, kMaxUiScale);
}

}

void Shortcuts::SetUiScale(float scale)
{
	m_settings.Get().uiScale = NormalizeUiScale(scale);
	m_settings.MarkDirty();
}

void Shortcuts::Process()
{
	const float current = m_settings.Get().uiScale;
	if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Equal) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd))
		SetUiScale(current + kUiScaleStep);
	if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Minus) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract))
		SetUiScale(m_settings.Get().uiScale - kUiScaleStep);
	if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_0) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Keypad0))
		SetUiScale(1.0f);
	if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
		m_navigator.ShowPage(Page::Discover);
		m_discover.FocusSearch();
	}
}

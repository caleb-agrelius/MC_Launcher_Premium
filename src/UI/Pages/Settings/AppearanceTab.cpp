#include "UI/Pages/Settings/AppearanceTab.h"

#include "UI/Pages/Settings/ThemeEditor.h"
#include "Storage/Settings.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using Theme::S;

bool AppearanceTab::Draw(Settings& settings, const SettingsPageContext&, SettingsLayout& ui)
{
	bool changed = false;

	ui.BeginCard("theme_card", true);
	ui.CardTitle("Theme", "Pick a color theme, or create your own. It applies right away.");
	const float spacing = ImGui::GetStyle().ItemSpacing.x * 1.5f;
	const float available = ImGui::GetContentRegionAvail().x;
	const int columns = std::max(1, static_cast<int>((available + spacing) / (S(230.0f) + spacing)));
	const float cardWidth = (available - spacing * (columns - 1)) / columns;
	const std::size_t current = Theme::FindTheme(settings.theme);
	const std::size_t count = Theme::ThemeCount();
	for (std::size_t i = 0; i <= count; ++i) {
		if (i % columns != 0)
			ImGui::SameLine(0.0f, spacing);
		if (i == count) {
			if (Widgets::NewThemeCard(cardWidth)) {
				settings.customThemes.push_back(ThemeEditor::NewCustomTheme(settings, Theme::Themes()[current]));
				settings.theme = settings.customThemes.back().id;
				changed = true;
			}
		} else if (Widgets::ThemeCard(Theme::Themes()[i], i == current, cardWidth) && i != current) {
			settings.theme = Theme::Themes()[i].id;
			changed = true;
		}
	}
	ui.EndCard();

	changed |= m_themeEditor.Draw(settings, ui);

	ui.RowsCard("display_card", "Display", [&] {
		ui.Row("UI scale", "On top of your Windows display scaling. Also Ctrl +, Ctrl - and Ctrl 0.");
		// Applied when the slider is released: rescaling mid-drag would move the slider under the mouse.
		static int dragPercent = 0;
		static bool dragging = false;
		int percent = dragging ? dragPercent : static_cast<int>(std::lround(settings.uiScale * 100.0f));
		const float resetWidth = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - resetWidth - ImGui::GetStyle().ItemSpacing.x);
		if (ImGui::SliderInt("##ui_scale", &percent, static_cast<int>(kMinUiScale * 100), static_cast<int>(kMaxUiScale * 100), "%d%%"))
			dragPercent = (percent + 2) / 5 * 5;
		if (ImGui::IsItemActivated())
			dragPercent = percent;
		dragging = ImGui::IsItemActive();
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			settings.uiScale = std::clamp(dragPercent / 100.0f, kMinUiScale, kMaxUiScale);
			changed = true;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(settings.uiScale == 1.0f);
		if (ImGui::Button("Reset")) {
			settings.uiScale = 1.0f;
			changed = true;
		}
		ImGui::EndDisabled();

		ui.Row("Reduce motion", "No page transitions, fade-ins, sliding notifications or smooth scrolling.");
		changed |= Widgets::Toggle("##reduce_motion", &settings.reduceMotion);

		ui.Row("Gallery banners", "Show screenshots behind the selected pack. Turn off to save bandwidth.");
		changed |= Widgets::Toggle("##gallery", &settings.showGalleryBanners);
	});
	return changed;
}

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

// A new custom theme, copied from `base`, with an unused id and name.
CustomTheme ThemeEditor::NewCustomTheme(const Settings& settings, const Theme::ThemeInfo& base)
{
	int number = 1;
	auto taken = [&](const std::string& id) {
		for (const CustomTheme& theme : settings.customThemes) {
			if (theme.id == id)
				return true;
		}
		return false;
	};
	while (taken("custom-" + std::to_string(number)))
		++number;
	CustomTheme theme;
	theme.id = "custom-" + std::to_string(number);
	theme.name = "My theme " + std::to_string(number);
	Theme::PaletteToCustom(base.palette, theme);
	return theme;
}

bool ThemeEditor::Draw(Settings& settings, SettingsLayout& ui)
{
	auto it = std::find_if(settings.customThemes.begin(), settings.customThemes.end(),
		[&](const CustomTheme& theme) { return theme.id == settings.theme; });
	if (it == settings.customThemes.end())
		return false;
	CustomTheme& theme = *it;
	bool changed = false;

	ui.BeginCard("theme_editor", true);
	ui.CardTitle("Edit your theme", "Changes apply as you make them and are saved automatically.");
	if (ui.BeginRows("theme_editor_rows")) {
		ui.Row("Name");
		changed |= ImGui::InputText("##theme_name", &theme.name);

		ui.Row("Start from", "Copies another theme's colors into this one.");
		static int startFrom = 0;
		std::vector<const char*> names;
		for (std::size_t i = 0; i < Theme::BuiltInThemeCount(); ++i)
			names.push_back(Theme::Themes()[i].name.c_str());
		const float applyWidth = ImGui::CalcTextSize("Copy colors").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - applyWidth - ImGui::GetStyle().ItemSpacing.x);
		Widgets::Select("##start_from", &startFrom, names.data(), static_cast<int>(names.size()));
		ImGui::SameLine();
		if (ImGui::Button("Copy colors")) {
			Theme::PaletteToCustom(Theme::Themes()[static_cast<std::size_t>(startFrom)].palette, theme);
			changed = true;
		}

		ui.Row("Light theme", "Use with light backgrounds and dark text.");
		changed |= Widgets::Toggle("##theme_light", &theme.light);

		const auto& fields = Theme::PaletteFields();
		for (std::size_t i = 0; i < fields.size(); ++i) {
			ui.Row(fields[i].label);
			ImGui::PushID(static_cast<int>(i));
			changed |= ImGui::ColorEdit3("##color", &theme.colors[i * 3],
				ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB);
			ImGui::PopID();
		}
		ui.EndRows();
	}
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	if (ImGui::Button("Duplicate")) {
		const Theme::ThemeInfo copySource{ theme.id, theme.name, "", Theme::PaletteFromCustom(theme), true };
		CustomTheme copy = NewCustomTheme(settings, copySource);
		copy.name = theme.name + " copy";
		settings.customThemes.push_back(std::move(copy)); // invalidates `theme`
		settings.theme = settings.customThemes.back().id;
		ui.EndCard();
		return true;
	}
	ImGui::SameLine();
	if (Widgets::DangerButton("Delete theme")) {
		settings.customThemes.erase(it);
		settings.theme = Theme::Themes()[0].id;
		changed = true;
	}
	ui.EndCard();
	return changed;
}

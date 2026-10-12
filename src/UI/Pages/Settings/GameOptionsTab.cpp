#include "UI/Pages/Settings/GameOptionsTab.h"

#include "Game/GameOptions.h"
#include "Storage/Settings.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using Theme::S;

bool GameOptionsTab::Draw(Settings& settings, const SettingsPageContext&, SettingsLayout& ui)
{
	bool changed = false;
	GameOptionsSettings& options = settings.gameOptions;

	ui.Notice("Turn an option on to apply it to every pack when you press Play. Options left off keep "
		"whatever the pack or your in-game settings use.", Theme::C().info);

	ui.RowsCard("game_options_mode", "How they're applied", [&] {
		ui.Row("Apply", "Every launch keeps all packs in sync but undoes in-game changes to these options.");
		int mode = options.everyLaunch ? 0 : 1;
		const char* modes[] = { "Every time a pack starts", "Only the first time each pack starts" };
		if (Widgets::Select("##apply_mode", &mode, modes, 2)) {
			options.everyLaunch = mode == 0;
			changed = true;
		}
		ui.Row("Options turned on");
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%d", static_cast<int>(options.values.size()));
		ImGui::SameLine(0.0f, S(16.0f));
		ImGui::BeginDisabled(options.values.empty());
		if (ImGui::Button("Turn all off")) {
			options.values.clear();
			changed = true;
		}
		ImGui::EndDisabled();
	});

	const char* groups[] = { "Video", "Controls", "Sound", "Chat & accessibility", "First launch" };
	for (const char* group : groups) {
		ImGui::PushID(group);
		ui.RowsCard("group", group, [&] {
			for (const GameOptionDef& def : GameOptionDefs()) {
				if (std::string(def.group) != group)
					continue;
				ImGui::PushID(def.key);
				ui.Row(def.label, def.hint);
				auto found = options.values.find(def.key);
				bool on = found != options.values.end();
				if (Widgets::Toggle("##on", &on)) {
					if (on)
						options.values[def.key] = def.defaultValue;
					else
						options.values.erase(def.key);
					changed = true;
				}
				ImGui::SetItemTooltip(on ? "Applied to every pack" : "Not applied: packs keep their own setting");
				found = options.values.find(def.key);
				std::string value = found != options.values.end() ? found->second : std::string(def.defaultValue);
				ImGui::SameLine(0.0f, S(16.0f));
				ImGui::BeginDisabled(!on);
				ImGui::SetNextItemWidth(-FLT_MIN);
				bool edited = false;
				switch (def.type) {
				case GameOptionType::Toggle: {
					bool checked = (value == "true") != def.inverted;
					if (Widgets::Toggle("##value", &checked)) {
						value = (checked != def.inverted) ? "true" : "false";
						edited = true;
					}
					break;
				}
				case GameOptionType::Slider: {
					float display = GameOptionToDisplay(def, value);
					const bool unlimited = std::string(def.key) == "maxFps" && display >= def.max - 0.5f;
					if (ImGui::SliderFloat("##value", &display, def.min, def.max, unlimited ? "Unlimited" : def.format)) {
						value = GameOptionFromDisplay(def, display);
						edited = true;
					}
					break;
				}
				case GameOptionType::Choice: {
					int index = 0;
					std::vector<const char*> labels;
					for (std::size_t i = 0; i < def.choices.size(); ++i) {
						labels.push_back(def.choices[i].label);
						if (value == def.choices[i].value)
							index = static_cast<int>(i);
					}
					if (Widgets::Select("##value", &index, labels.data(), static_cast<int>(labels.size()))) {
						value = def.choices[static_cast<std::size_t>(index)].value;
						edited = true;
					}
					break;
				}
				case GameOptionType::Text:
					if (ImGui::InputText("##value", &value, ImGuiInputTextFlags_CharsNoBlank)) {
						value.erase(std::remove(value.begin(), value.end(), ':'), value.end());
						edited = true;
					}
					break;
				}
				ImGui::EndDisabled();
				if (edited && on) {
					options.values[def.key] = value;
					changed = true;
				}
				ImGui::PopID();
			}
		});
		ImGui::PopID();
	}
	return changed;
}

#include "UI/Pages/Settings/AboutTab.h"

#include "Storage/Settings.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using Theme::S;

bool AboutTab::Draw(Settings&, const SettingsPageContext& context, SettingsLayout& ui)
{
	ui.BeginCard("about_card");
	Widgets::LogoBlock(S(64.0f));
	ImGui::SameLine(0.0f, S(16.0f));
	ImGui::BeginGroup();
	ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
	Widgets::Wordmark("MC LAUNCHER", std::floor(S(3.0f)), Theme::Mix(Theme::C().text, Theme::C().accent, 0.1f),
		Theme::Mix(Theme::C().accentDeep, Theme::C().windowBg, 0.15f));
	ImGui::TextDisabled("Premium edition");
	ImGui::EndGroup();
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	if (ui.BeginRows("about_rows")) {
		ui.TextRow("Version", context.appVersion.c_str());
		ui.TextRow("Display scaling", (std::to_string(static_cast<int>(std::lround(context.dpiScale * 100.0f))) + "% (from Windows)").c_str());
		ui.TextRow("Memory", context.systemMemoryMb > 0 ? ui.FormatGb(static_cast<int>(context.systemMemoryMb)).c_str() : "Unknown");
		ui.FolderRow("Data folder", "settings.json, app.log, downloads and installs.", context.dataFolder);
		ui.EndRows();
	}
	ui.EndCard();

	ui.RowsCard("shortcuts_card", "Keyboard shortcuts", [&] {
		const char* shortcuts[][2] = {
			{ "Ctrl F", "Search modpacks" },
			{ "Ctrl +  /  Ctrl -", "Make the UI larger / smaller" },
			{ "Ctrl 0", "Reset the UI scale" },
		};
		for (const auto& shortcut : shortcuts) {
			ui.Row(shortcut[0]);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", shortcut[1]);
		}
	});

	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextDisabled("Modpack data and images come from Modrinth, CurseForge, FTB, Technic and ATLauncher. "
		"Not affiliated with Mojang or Microsoft.");
	ImGui::PopTextWrapPos();
	return false;
}

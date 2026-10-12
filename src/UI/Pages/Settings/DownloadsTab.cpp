#include "UI/Pages/Settings/DownloadsTab.h"

#include "Storage/Settings.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>

using Theme::S;

bool DownloadsTab::Draw(Settings& settings, const SettingsPageContext& context, SettingsLayout& ui)
{
	bool changed = false;
	ui.RowsCard("search_card", "Search", [&] {
		ui.Row("Results per search", "How many packs each search shows. CurseForge shows at most 50.");
		changed |= ImGui::SliderInt("##search_limit", &settings.searchLimit, kMinSearchLimit, kMaxSearchLimit);
		settings.searchLimit = std::clamp(settings.searchLimit, kMinSearchLimit, kMaxSearchLimit);
	});

	ui.RowsCard("curseforge_card", "CurseForge", "Browsing and installing CurseForge packs needs your own API key from "
		"console.curseforge.com. It's saved in settings.json in the data folder.", [&] {
		ui.Row("API key", context.curseForgeEnvKeySet
			? "Leave empty to use the MCLP_CURSEFORGE_API_KEY environment variable, which is set."
			: "Or leave empty and set the MCLP_CURSEFORGE_API_KEY environment variable.");
		static bool showKey = false;
		const float showWidth = ImGui::CalcTextSize("Hide").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - showWidth - ImGui::GetStyle().ItemSpacing.x);
		changed |= ImGui::InputTextWithHint("##curseforge_key", context.curseForgeEnvKeySet
			? "Using the environment variable" : "Not set", &settings.curseForgeApiKey,
			showKey ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_Password);
		ImGui::SameLine();
		if (ImGui::Button(showKey ? "Hide" : "Show", ImVec2(showWidth, 0.0f)))
			showKey = !showKey;
	});

	ui.RowsCard("sharing_card", "Shared mod files", "Mods and resource and shader packs that several packs use are kept once on "
		"disk, as hard links to one copy in minecraft/shared-files. Each pack still sees ordinary files: removing or "
		"turning off a mod in one pack doesn't touch the others. Explorer counts each link at full size, so folder "
		"sizes don't show the saving. Packs on another drive than the launcher get their own copies.", [&] {
		ui.Row("Share identical files", "Off: new installs get their own copies. Files already shared stay shared.");
		changed |= Widgets::Toggle("##share_mod_files", &settings.shareModFiles);
		ui.Row("Packs already installed", "Finds identical mods in your packs and servers and keeps one copy of each. "
			"Close running games first, since their mods are in use.");
		if (context.sharingStats.empty() && !context.sharingBusy && context.countSharing)
			context.countSharing();
		ImGui::BeginDisabled(!settings.shareModFiles || context.sharingBusy || context.sharingBlocked);
		if (ImGui::Button("Share files packs already have") && context.shareExisting)
			context.shareExisting();
		ImGui::EndDisabled();
		if (context.sharingBlocked)
			ImGui::SetItemTooltip("Close Minecraft first: its mods are in use.");
		if (context.sharingBusy) {
			ImGui::SameLine();
			Widgets::Spinner(Theme::C().accent);
		}
		ImGui::PushFont(nullptr, Theme::kFontSmall);
		if (!context.sharingStatus.empty())
			ImGui::TextWrapped("%s", context.sharingStatus.c_str());
		if (!context.sharingStats.empty())
			ImGui::TextDisabled("%s", context.sharingStats.c_str());
		ImGui::PopFont();
	});

	ui.RowsCard("folders_card", "Folders", [&] {
		const std::filesystem::path root(context.dataFolder);
		ui.FolderRow("Downloads", "Downloaded .mrpack files.", (root / "downloads").string());
		ui.FolderRow("Installs", "Extracted packs.", (root / "installs").string());
		ui.FolderRow("Instances", "Each pack's game folder: mods, config, saves, logs.", (root / "instances").string());
		ui.FolderRow("Minecraft files", "Shared versions, libraries, assets and Java runtimes.", (root / "minecraft").string());
	});
	return changed;
}

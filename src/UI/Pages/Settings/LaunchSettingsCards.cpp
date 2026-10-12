#include "UI/Pages/Settings/LaunchSettingsCards.h"

#include "Game/JavaFinder.h"
#include "Storage/Settings.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <vector>

using Theme::S;

bool LaunchSettingsCards::DrawJavaChoice(LaunchSettings& launch, const SettingsPageContext& context)
{
	if (!context.javasScanned && !context.javasScanning && context.scanJavas)
		context.scanJavas();
	static const std::vector<JavaInstall> kNone;
	const std::vector<JavaInstall>& javas = context.javas ? *context.javas : kNone;
	// The detected Java the setting names, if any.
	const JavaInstall* chosen = nullptr;
	if (!launch.javaPath.empty()) {
		const std::filesystem::path path(launch.javaPath);
		for (const JavaInstall& java : javas) {
			if (path == java.javaw || path == java.java || path == java.home)
				chosen = &java;
		}
	}
	// "Custom path" stays open while typing, even before the path names a file.
	ImGuiStorage* storage = ImGui::GetStateStorage();
	const ImGuiID customKey = ImGui::GetID("java_custom");
	bool custom = !launch.javaPath.empty() && !chosen;
	custom = custom || storage->GetBool(customKey, false);

	bool changed = false;
	const std::string preview = custom ? "Custom path" : chosen ? JavaLabel(*chosen) : "Automatic";
	const float rescanWidth = Widgets::ButtonWidth("Rescan");
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - rescanWidth - ImGui::GetStyle().ItemSpacing.x);
	if (Widgets::BeginSelect("##java", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
		if (Widgets::ListItem("Automatic", !custom && !chosen, true, "matches each Minecraft version")) {
			launch.javaPath.clear();
			storage->SetBool(customKey, false);
			changed = true;
		}
		for (std::size_t i = 0; i < javas.size(); ++i) {
			const JavaInstall& java = javas[i];
			ImGui::PushID(static_cast<int>(i));
			const std::string folder = java.home.filename().string();
			if (Widgets::ListItem(JavaLabel(java).c_str(), &java == chosen && !custom, true, folder.c_str())) {
				launch.javaPath = java.javaw.string();
				storage->SetBool(customKey, false);
				changed = true;
			}
			ImGui::SetItemTooltip("%s", java.home.string().c_str());
			ImGui::PopID();
		}
		if (Widgets::ListItem("Custom path...", custom)) {
			storage->SetBool(customKey, true);
			if (chosen) {
				launch.javaPath.clear();
				changed = true;
			}
		}
		Widgets::EndSelect();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(context.javasScanning);
	if (ImGui::Button("Rescan") && context.scanJavas)
		context.scanJavas();
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Look for newly installed Javas");

	ImGui::PushFont(nullptr, Theme::kFontSmall);
	ImGui::PushTextWrapPos(0.0f);
	if (custom) {
		changed |= ImGui::InputTextWithHint("##java_path", "Path to java.exe, javaw.exe, or a Java folder", &launch.javaPath);
		std::error_code ec;
		if (!launch.javaPath.empty() && !std::filesystem::exists(std::filesystem::path(launch.javaPath), ec))
			ImGui::TextColored(Theme::C().error, "Not found. Play will fail until this is fixed or cleared.");
	} else if (context.javasScanning) {
		ImGui::TextDisabled("Looking for Java on this PC...");
	} else if (!chosen) {
		std::string found;
		std::vector<int> majors;
		for (const JavaInstall& java : javas) {
			if (std::find(majors.begin(), majors.end(), java.major) == majors.end())
				majors.push_back(java.major);
		}
		for (int major : majors)
			found += (found.empty() ? "" : ", ") + std::to_string(major);
		if (found.empty())
			ImGui::TextDisabled("No Java found on this PC; Mojang's is downloaded for each Minecraft version.");
		else
			ImGui::TextDisabled("Found Java %s on this PC. Other versions are downloaded from Mojang when needed.", found.c_str());
	} else {
		ImGui::TextDisabled("%s", chosen->home.string().c_str());
	}
	ImGui::PopTextWrapPos();
	ImGui::PopFont();
	return changed;
}

bool LaunchSettingsCards::Draw(LaunchSettings& launch, const SettingsPageContext& context, SettingsLayout& ui, bool global)
{
	bool changed = false;
	ui.BeginCard("memory_card");
	ui.CardTitle("Memory", "How much RAM Minecraft may use. Most modpacks run well with 4\xE2\x80\x93" "8 GB.");
	const int systemMb = context.systemMemoryMb > 0 ? static_cast<int>(context.systemMemoryMb) : kMaxMemoryMb;
	const int sliderMaxMb = std::clamp(systemMb, kMinMemoryMb * 2, kMaxMemoryMb);
	if (ui.BeginRows("memory_rows")) {
		ui.Row("Maximum memory", "Java's -Xmx.");
		float maxGb = launch.maxMemoryMb / 1024.0f;
		if (ImGui::SliderFloat("##max_memory", &maxGb, kMinMemoryMb / 1024.0f, sliderMaxMb / 1024.0f, "%.1f GB")) {
			launch.maxMemoryMb = std::clamp(static_cast<int>(std::lround(maxGb * 2.0f)) * 512, kMinMemoryMb, kMaxMemoryMb);
			launch.minMemoryMb = std::min(launch.minMemoryMb, launch.maxMemoryMb);
			changed = true;
		}
		if (context.systemMemoryMb > 0) {
			ImGui::PushFont(nullptr, Theme::kFontSmall);
			if (launch.maxMemoryMb > static_cast<int>(context.systemMemoryMb * 3 / 4))
				ImGui::TextColored(Theme::C().error, "That's over 75%% of this PC's %s. Windows may slow down.", ui.FormatGb(systemMb).c_str());
			else
				ImGui::TextDisabled("This PC has %s.", ui.FormatGb(systemMb).c_str());
			ImGui::PopFont();
		}

		ui.Row("Starting memory", "Java's -Xms. Automatic lets Java decide; a fixed amount can smooth out early stutters.");
		float minGb = launch.minMemoryMb / 1024.0f;
		if (ImGui::SliderFloat("##min_memory", &minGb, 0.0f, launch.maxMemoryMb / 1024.0f,
				launch.minMemoryMb == 0 ? "Automatic" : "%.1f GB")) {
			launch.minMemoryMb = std::clamp(static_cast<int>(std::lround(minGb * 2.0f)) * 512, 0, launch.maxMemoryMb);
			changed = true;
		}
		ui.EndRows();
	}
	ui.EndCard();

	ui.RowsCard("java_card", "Java", [&] {
		ui.Row("Java", "Automatic uses an installed Java with the version each Minecraft version asks for "
			"(for example 8 for 1.16, 17 for 1.18, 21 for 1.21), and downloads Mojang's when none matches.");
		changed |= DrawJavaChoice(launch, context);

		ui.Row("JVM arguments", "Extra arguments for Java, separated by spaces. Use quotes for values with spaces.");
		changed |= ImGui::InputTextWithHint("##jvm_args", "e.g. -XX:+UseG1GC -XX:MaxGCPauseMillis=50", &launch.jvmArgs);
	});

	ui.BeginCard("window_card");
	ui.CardTitle("Game window");
	if (ui.BeginRows("window_rows")) {
		ui.Row("Fullscreen");
		changed |= Widgets::Toggle("##fullscreen", &launch.fullscreen);

		ui.Row("Custom window size", "Off uses Minecraft's default size (854 x 480).");
		ImGui::BeginDisabled(launch.fullscreen);
		bool customSize = launch.windowWidth > 0 && launch.windowHeight > 0;
		if (Widgets::Toggle("##custom_size", &customSize)) {
			launch.windowWidth = customSize ? 1280 : 0;
			launch.windowHeight = customSize ? 720 : 0;
			changed = true;
		}
		if (customSize) {
			ImGui::SameLine(0.0f, S(16.0f));
			const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f -
				ImGui::CalcTextSize("x").x) * 0.5f;
			ImGui::SetNextItemWidth(half);
			if (ImGui::InputInt("##width", &launch.windowWidth, 0)) {
				launch.windowWidth = std::max(kMinWindowSide, launch.windowWidth);
				changed = true;
			}
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("x");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(half);
			if (ImGui::InputInt("##height", &launch.windowHeight, 0)) {
				launch.windowHeight = std::max(kMinWindowSide, launch.windowHeight);
				changed = true;
			}
		}
		ImGui::EndDisabled();

		if (global) {
			ui.Row("When the game starts");
			const char* options[] = { "Keep the launcher open", "Minimize the launcher", "Close the launcher" };
			int afterLaunch = static_cast<int>(launch.afterLaunch);
			if (Widgets::Select("##after_launch", &afterLaunch, options, 3)) {
				launch.afterLaunch = static_cast<AfterLaunch>(afterLaunch);
				changed = true;
			}
			if (launch.afterLaunch == AfterLaunch::Minimize) {
				ImGui::PushFont(nullptr, Theme::kFontSmall);
				ImGui::TextDisabled("The launcher comes back when Minecraft closes.");
				ImGui::PopFont();
			}
		}
		ui.EndRows();
	}
	if (global) {
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		if (ImGui::Button("Reset Minecraft settings to defaults")) {
			launch = LaunchSettings{};
			changed = true;
		}
	}
	ui.EndCard();
	return changed;
}

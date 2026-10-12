#include "SettingsPage.h"

#include "Platform.h"
#include "Storage/Settings.h"
#include "Theme.h"
#include "Widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

using Theme::S;

namespace {

enum class Tab { Appearance, Minecraft, Account, Downloads, About };

const char* TabName(Tab tab)
{
	switch (tab) {
	case Tab::Appearance: return "Appearance";
	case Tab::Minecraft: return "Minecraft";
	case Tab::Account: return "Account";
	case Tab::Downloads: return "Downloads";
	case Tab::About: return "About";
	}
	return "";
}

Tab g_tab = Tab::Appearance;

// Pill-style tab strip that switches g_tab.
void TabStrip()
{
	const Tab tabs[] = { Tab::Appearance, Tab::Minecraft, Tab::Account, Tab::Downloads, Tab::About };
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(18.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(18.0f), S(8.0f)));
	for (Tab tab : tabs) {
		const bool selected = g_tab == tab;
		ImGui::PushStyleColor(ImGuiCol_Button, selected ? Theme::C().accentDeep : Theme::C().frameBg);
		ImGui::PushFont(selected ? Theme::GetFonts().bold : Theme::GetFonts().regular, 0.0f);
		if (ImGui::Button(TabName(tab)))
			g_tab = tab;
		ImGui::PopFont();
		ImGui::PopStyleColor();
		ImGui::SameLine();
	}
	ImGui::NewLine();
	ImGui::PopStyleVar(2);
}

void BeginCard(const char* id)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::C().cardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22.0f), S(18.0f)));
	ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleVar();
}

void EndCard()
{
	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
}

void CardTitle(const char* title, const char* subtitle = nullptr)
{
	Widgets::Text(Theme::GetFonts().bold, Theme::kFontHeading, Theme::C().text, title);
	if (subtitle) {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", subtitle);
		ImGui::PopTextWrapPos();
	}
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
}

// Two-column label/control rows inside a card.
bool BeginRows(const char* id)
{
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(6.0f), S(9.0f)));
	if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
		ImGui::PopStyleVar();
		return false;
	}
	ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, S(240.0f));
	ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
	return true;
}

// Starts a row: label (and an optional hint under it) on the left; leaves the cursor in
// the control column with the next item set to fill it.
void Row(const char* label, const char* hint = nullptr)
{
	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::AlignTextToFramePadding();
	ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
	ImGui::TextUnformatted(label);
	ImGui::PopFont();
	if (hint) {
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(220.0f));
		ImGui::PushFont(nullptr, Theme::kFontSmall);
		ImGui::TextDisabled("%s", hint);
		ImGui::PopFont();
		ImGui::PopTextWrapPos();
	}
	ImGui::TableNextColumn();
	ImGui::SetNextItemWidth(-FLT_MIN);
}

void EndRows()
{
	ImGui::EndTable();
	ImGui::PopStyleVar();
}

// A tinted notice box, e.g. for placeholder sections.
void Notice(const char* text, const ImVec4& tint)
{
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->ChannelsSplit(2);
	drawList->ChannelsSetCurrent(1);
	ImGui::SetCursorScreenPos(ImVec2(pos.x + S(16.0f), pos.y + S(10.0f)));
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - S(32.0f));
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
	const float height = ImGui::GetItemRectMax().y - pos.y + S(10.0f);
	drawList->ChannelsSetCurrent(0);
	drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(Theme::WithAlpha(tint, 0.14f)), S(8.0f));
	drawList->AddRectFilled(pos, ImVec2(pos.x + S(4.0f), pos.y + height), ImGui::GetColorU32(tint), S(2.0f));
	drawList->ChannelsMerge();
	ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height));
	ImGui::Dummy(ImVec2(width, S(8.0f)));
}

// A read-only path with an "Open" button.
void FolderRow(const char* label, const char* hint, const std::string& path)
{
	Row(label, hint);
	const float buttonWidth = ImGui::CalcTextSize("Open").x + ImGui::GetStyle().FramePadding.x * 2.0f;
	std::string display = path;
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
	ImGui::PushID(label);
	ImGui::InputText("##path", &display, ImGuiInputTextFlags_ReadOnly);
	ImGui::SameLine();
	if (ImGui::Button("Open"))
		Platform::OpenFolder(path);
	ImGui::PopID();
}

std::string FormatGb(int mb)
{
	char text[32];
	std::snprintf(text, sizeof(text), "%.1f GB", mb / 1024.0);
	return text;
}

bool DrawAppearance(Settings& settings)
{
	bool changed = false;

	BeginCard("theme_card");
	CardTitle("Theme", "Pick a color theme. It applies right away.");
	const float spacing = ImGui::GetStyle().ItemSpacing.x * 1.5f;
	const float available = ImGui::GetContentRegionAvail().x;
	const int columns = std::max(1, static_cast<int>((available + spacing) / (S(230.0f) + spacing)));
	const float cardWidth = (available - spacing * (columns - 1)) / columns;
	const std::size_t current = Theme::FindTheme(settings.theme);
	for (std::size_t i = 0; i < Theme::ThemeCount(); ++i) {
		if (i % columns != 0)
			ImGui::SameLine(0.0f, spacing);
		if (Widgets::ThemeCard(Theme::Themes()[i], i == current, cardWidth) && i != current) {
			settings.theme = Theme::Themes()[i].id;
			changed = true;
		}
	}
	EndCard();

	BeginCard("display_card");
	CardTitle("Display");
	if (BeginRows("display_rows")) {
		Row("UI scale", "On top of your Windows display scaling. Also Ctrl +, Ctrl - and Ctrl 0.");
		int percent = static_cast<int>(std::lround(settings.uiScale * 100.0f));
		const float resetWidth = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - resetWidth - ImGui::GetStyle().ItemSpacing.x);
		if (ImGui::SliderInt("##ui_scale", &percent, static_cast<int>(kMinUiScale * 100), static_cast<int>(kMaxUiScale * 100), "%d%%")) {
			percent = (percent + 2) / 5 * 5;
			settings.uiScale = std::clamp(percent / 100.0f, kMinUiScale, kMaxUiScale);
			changed = true;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(settings.uiScale == 1.0f);
		if (ImGui::Button("Reset")) {
			settings.uiScale = 1.0f;
			changed = true;
		}
		ImGui::EndDisabled();

		Row("Reduce motion", "Show images and notifications without fading or sliding in.");
		changed |= Widgets::Toggle("##reduce_motion", &settings.reduceMotion);

		Row("Gallery banners", "Show screenshots behind the selected pack. Turn off to save bandwidth.");
		changed |= Widgets::Toggle("##gallery", &settings.showGalleryBanners);
		EndRows();
	}
	EndCard();
	return changed;
}

bool DrawMinecraft(Settings& settings, const SettingsPageContext& context)
{
	bool changed = false;
	LaunchSettings& launch = settings.launch;

	Notice("These apply to every pack the next time you press Play.", Theme::C().info);

	BeginCard("memory_card");
	CardTitle("Memory", "How much RAM Minecraft may use. Most modpacks run well with 4\xE2\x80\x93" "8 GB.");
	const int systemMb = context.systemMemoryMb > 0 ? static_cast<int>(context.systemMemoryMb) : kMaxMemoryMb;
	const int sliderMaxMb = std::clamp(systemMb, kMinMemoryMb * 2, kMaxMemoryMb);
	if (BeginRows("memory_rows")) {
		Row("Maximum memory", "Java's -Xmx.");
		float maxGb = launch.maxMemoryMb / 1024.0f;
		if (ImGui::SliderFloat("##max_memory", &maxGb, kMinMemoryMb / 1024.0f, sliderMaxMb / 1024.0f, "%.1f GB")) {
			launch.maxMemoryMb = std::clamp(static_cast<int>(std::lround(maxGb * 2.0f)) * 512, kMinMemoryMb, kMaxMemoryMb);
			launch.minMemoryMb = std::min(launch.minMemoryMb, launch.maxMemoryMb);
			changed = true;
		}
		if (context.systemMemoryMb > 0) {
			ImGui::PushFont(nullptr, Theme::kFontSmall);
			if (launch.maxMemoryMb > static_cast<int>(context.systemMemoryMb * 3 / 4))
				ImGui::TextColored(Theme::C().error, "That's over 75%% of this PC's %s. Windows may slow down.", FormatGb(systemMb).c_str());
			else
				ImGui::TextDisabled("This PC has %s.", FormatGb(systemMb).c_str());
			ImGui::PopFont();
		}

		Row("Starting memory", "Java's -Xms. Automatic lets Java decide; a fixed amount can smooth out early stutters.");
		float minGb = launch.minMemoryMb / 1024.0f;
		if (ImGui::SliderFloat("##min_memory", &minGb, 0.0f, launch.maxMemoryMb / 1024.0f,
				launch.minMemoryMb == 0 ? "Automatic" : "%.1f GB")) {
			launch.minMemoryMb = std::clamp(static_cast<int>(std::lround(minGb * 2.0f)) * 512, 0, launch.maxMemoryMb);
			changed = true;
		}
		EndRows();
	}
	EndCard();

	BeginCard("java_card");
	CardTitle("Java");
	if (BeginRows("java_rows")) {
		Row("Java executable", "Leave empty to use Mojang's Java for each Minecraft version (downloaded automatically). "
			"Otherwise, the path to java.exe, javaw.exe, or a Java folder.");
		changed |= ImGui::InputTextWithHint("##java_path", "Automatic (Mojang's Java)", &launch.javaPath);
		if (!launch.javaPath.empty()) {
			std::error_code ec;
			const std::filesystem::path java(launch.javaPath);
			if (!std::filesystem::exists(java, ec)) {
				ImGui::PushFont(nullptr, Theme::kFontSmall);
				ImGui::TextColored(Theme::C().error, "Not found. Play will fail until this is fixed or cleared.");
				ImGui::PopFont();
			}
		}

		Row("JVM arguments", "Extra arguments for Java, separated by spaces. Use quotes for values with spaces.");
		changed |= ImGui::InputTextWithHint("##jvm_args", "e.g. -XX:+UseG1GC -XX:MaxGCPauseMillis=50", &launch.jvmArgs);
		EndRows();
	}
	EndCard();

	BeginCard("window_card");
	CardTitle("Game window");
	if (BeginRows("window_rows")) {
		Row("Fullscreen");
		changed |= Widgets::Toggle("##fullscreen", &launch.fullscreen);

		Row("Custom window size", "Off uses Minecraft's default size (854 x 480).");
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

		Row("When the game starts");
		const char* options[] = { "Keep the launcher open", "Minimize the launcher", "Close the launcher" };
		int afterLaunch = static_cast<int>(launch.afterLaunch);
		if (ImGui::Combo("##after_launch", &afterLaunch, options, 3)) {
			launch.afterLaunch = static_cast<AfterLaunch>(afterLaunch);
			changed = true;
		}
		if (launch.afterLaunch == AfterLaunch::Minimize) {
			ImGui::PushFont(nullptr, Theme::kFontSmall);
			ImGui::TextDisabled("The launcher comes back when Minecraft closes.");
			ImGui::PopFont();
		}
		EndRows();
	}
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	if (ImGui::Button("Reset Minecraft settings to defaults")) {
		launch = LaunchSettings{};
		changed = true;
	}
	EndCard();
	return changed;
}

bool DrawAccount(Settings& settings, const SettingsPageContext& context)
{
	bool changed = false;
	BeginCard("account_card");
	CardTitle("Current account");
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float avatar = S(56.0f);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	// Steve-style head made of blocks.
	const float px = avatar / 8.0f;
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			ImVec4 color = y < 2 ? ImVec4(0.27f, 0.18f, 0.11f, 1.0f) : ImVec4(0.75f, 0.55f, 0.42f, 1.0f);
			if (y == 4 && (x == 1 || x == 2 || x == 5 || x == 6))
				color = x == 1 || x == 6 ? ImVec4(1, 1, 1, 1) : ImVec4(0.30f, 0.25f, 0.60f, 1.0f);
			if (y == 6 && x >= 3 && x <= 4)
				color = ImVec4(0.45f, 0.28f, 0.22f, 1.0f);
			drawList->AddRectFilled(ImVec2(pos.x + x * px, pos.y + y * px), ImVec2(pos.x + (x + 1) * px, pos.y + (y + 1) * px),
				ImGui::GetColorU32(color));
		}
	}
	ImGui::Dummy(ImVec2(avatar, avatar));
	ImGui::SameLine(0.0f, S(16.0f));
	ImGui::BeginGroup();
	ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
	ImGui::TextUnformatted(context.accountName.empty() ? "Not signed in" : context.accountName.c_str());
	ImGui::PopFont();
	ImGui::TextDisabled("%s", context.accountKind.c_str());
	ImGui::TextDisabled("Sign in, play offline, or sign out from the ACCOUNT section of the sidebar.");
	ImGui::EndGroup();
	EndCard();

	BeginCard("microsoft_card");
	CardTitle("Microsoft sign-in", "Signing in with Microsoft needs an Azure app (client) ID that Mojang has approved for the "
		"Minecraft API. Without one, only offline mode is available.");
	if (BeginRows("microsoft_rows")) {
		Row("Azure client ID", "Takes effect the next time you sign in.");
		changed |= ImGui::InputTextWithHint("##client_id", "00000000-0000-0000-0000-000000000000", &settings.microsoftClientId);
		EndRows();
	}
	EndCard();
	return changed;
}

bool DrawDownloads(Settings& settings, const SettingsPageContext& context)
{
	bool changed = false;
	BeginCard("search_card");
	CardTitle("Search");
	if (BeginRows("search_rows")) {
		Row("Results per search", "How many packs each search shows. CurseForge shows at most 50.");
		changed |= ImGui::SliderInt("##search_limit", &settings.searchLimit, kMinSearchLimit, kMaxSearchLimit);
		settings.searchLimit = std::clamp(settings.searchLimit, kMinSearchLimit, kMaxSearchLimit);
		EndRows();
	}
	EndCard();

	BeginCard("curseforge_card");
	CardTitle("CurseForge", "Browsing and installing CurseForge packs needs your own API key from "
		"console.curseforge.com. It's saved in settings.json in the data folder.");
	if (BeginRows("curseforge_rows")) {
		Row("API key", context.curseForgeEnvKeySet
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
		EndRows();
	}
	EndCard();

	BeginCard("folders_card");
	CardTitle("Folders");
	if (BeginRows("folder_rows")) {
		const std::filesystem::path root(context.dataFolder);
		FolderRow("Downloads", "Downloaded .mrpack files.", (root / "downloads").string());
		FolderRow("Installs", "Extracted packs.", (root / "installs").string());
		FolderRow("Instances", "Each pack's game folder: mods, config, saves, logs.", (root / "instances").string());
		FolderRow("Minecraft files", "Shared versions, libraries, assets and Java runtimes.", (root / "minecraft").string());
		EndRows();
	}
	EndCard();
	return changed;
}

void DrawAbout(const SettingsPageContext& context)
{
	BeginCard("about_card");
	CardTitle("MC Launcher Premium");
	if (BeginRows("about_rows")) {
		Row("Version");
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(context.appVersion.c_str());
		Row("Display scaling");
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%.0f%% (from Windows)", context.dpiScale * 100.0f);
		Row("Memory");
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(context.systemMemoryMb > 0 ? FormatGb(static_cast<int>(context.systemMemoryMb)).c_str() : "Unknown");
		FolderRow("Data folder", "settings.json, app.log, downloads and installs.", context.dataFolder);
		EndRows();
	}
	EndCard();

	BeginCard("shortcuts_card");
	CardTitle("Keyboard shortcuts");
	if (BeginRows("shortcut_rows")) {
		const char* shortcuts[][2] = {
			{ "Ctrl F", "Search modpacks" },
			{ "Ctrl +  /  Ctrl -", "Make the UI larger / smaller" },
			{ "Ctrl 0", "Reset the UI scale" },
		};
		for (const auto& shortcut : shortcuts) {
			Row(shortcut[0]);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", shortcut[1]);
		}
		EndRows();
	}
	EndCard();

	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextDisabled("Modpack data and images come from Modrinth, CurseForge, FTB, Technic and ATLauncher. "
		"Not affiliated with Mojang or Microsoft.");
	ImGui::PopTextWrapPos();
}

}

bool DrawSettingsPage(Settings& settings, const SettingsPageContext& context)
{
	TabStrip();
	ImGui::Spacing();

	bool changed = false;
	ImGui::BeginChild("settings_scroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
	switch (g_tab) {
	case Tab::Appearance: changed = DrawAppearance(settings); break;
	case Tab::Minecraft: changed = DrawMinecraft(settings, context); break;
	case Tab::Account: changed = DrawAccount(settings, context); break;
	case Tab::Downloads: changed = DrawDownloads(settings, context); break;
	case Tab::About: DrawAbout(context); break;
	}
	ImGui::EndChild();
	return changed;
}

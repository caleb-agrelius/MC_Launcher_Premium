#include "SettingsPage.h"

#include "Game/GameOptions.h"
#include "Game/JavaFinder.h"
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
#include <iterator>

using Theme::S;

namespace {

enum class Tab { Appearance, Minecraft, GameOptions, Account, Downloads, About };

const char* TabName(Tab tab)
{
	switch (tab) {
	case Tab::Appearance: return "Appearance";
	case Tab::Minecraft: return "Minecraft";
	case Tab::GameOptions: return "Game options";
	case Tab::Account: return "Account";
	case Tab::Downloads: return "Downloads";
	case Tab::About: return "About";
	}
	return "";
}

Tab g_tab = Tab::Appearance;
// Cards bubble in from this time: the later of opening the page and switching tabs.
double g_cardsShownAt = 0.0;
int g_cardIndex = 0; // cards drawn so far this frame, for the stagger

// Pill-style tab strip that switches g_tab.
void TabStrip()
{
	const Tab tabs[] = { Tab::Appearance, Tab::Minecraft, Tab::GameOptions, Tab::Account, Tab::Downloads, Tab::About };
	const char* labels[std::size(tabs)];
	int current = 0;
	for (std::size_t i = 0; i < std::size(tabs); ++i) {
		labels[i] = TabName(tabs[i]);
		if (tabs[i] == g_tab)
			current = static_cast<int>(i);
	}
	if (Widgets::TabStrip("settings_tabs", labels, static_cast<int>(std::size(tabs)), &current)) {
		g_tab = tabs[current];
		g_cardsShownAt = ImGui::GetTime();
	}
}

// Cards flow into columns on wide windows: each card goes to the shortest column (using
// the height BeginCard remembers), so the page needs less scrolling. Content drawn between
// cards starts a new section below every column.
struct CardFlow {
	bool active = false;
	int columns = 1;
	float columnWidth = 0.0f;
	float gap = 0.0f;
	float x = 0.0f;           // left edge, in screen space
	float columnY[3] = {};    // where each column's next card goes
	float afterCardY = -1.0f; // where EndCard left the cursor; anything lower means content came between
	int column = 0;           // the open card's column
	bool fullWidth = false;   // the open card spans every column
};
CardFlow g_flow;
constexpr float kMinColumnWidth = 520.0f; // unscaled

// Lays the cards drawn until EndCardFlow() out in as many columns as fit (up to three).
void BeginCardFlow()
{
	g_flow = CardFlow{};
	const float available = ImGui::GetContentRegionAvail().x;
	g_flow.gap = S(14.0f);
	g_flow.columns = std::clamp(static_cast<int>((available + g_flow.gap) / (S(kMinColumnWidth) + g_flow.gap)), 1, 3);
	if (g_flow.columns < 2)
		return;
	g_flow.active = true;
	g_flow.columnWidth = std::floor((available - g_flow.gap * static_cast<float>(g_flow.columns - 1)) / static_cast<float>(g_flow.columns));
	g_flow.x = ImGui::GetCursorScreenPos().x;
}

void EndCardFlow()
{
	g_flow.active = false;
}

// The bottom of the tallest column.
float FlowBottom()
{
	float bottom = g_flow.columnY[0];
	for (int i = 1; i < g_flow.columns; ++i)
		bottom = std::max(bottom, g_flow.columnY[i]);
	return bottom;
}

// fullWidth spans every column (wide content such as the theme grid).
void BeginCard(const char* id, bool fullWidth = false)
{
	float width = 0.0f;
	if (g_flow.active) {
		const float cursorY = ImGui::GetCursorScreenPos().y;
		if (g_flow.afterCardY < 0.0f || cursorY > g_flow.afterCardY + 0.5f) {
			// First card, or content was drawn after the last one: a new section starts here.
			for (float& y : g_flow.columnY)
				y = cursorY;
		}
		g_flow.fullWidth = fullWidth;
		g_flow.column = 0;
		float y = g_flow.columnY[0];
		if (fullWidth) {
			y = FlowBottom();
			width = g_flow.columnWidth * static_cast<float>(g_flow.columns) + g_flow.gap * static_cast<float>(g_flow.columns - 1);
		} else {
			for (int i = 1; i < g_flow.columns; ++i) {
				if (g_flow.columnY[i] < g_flow.columnY[g_flow.column] - 0.5f)
					g_flow.column = i;
			}
			y = g_flow.columnY[g_flow.column];
			width = g_flow.columnWidth;
		}
		ImGui::SetCursorScreenPos(ImVec2(g_flow.x + (fullWidth ? 0.0f : (g_flow.columnWidth + g_flow.gap) * static_cast<float>(g_flow.column)), y));
	}
	Widgets::BeginEntrance(g_cardsShownAt, g_cardIndex++);
	Widgets::BeginCard(id, ImVec2(S(22.0f), S(18.0f)), width);
}

void EndCard()
{
	Widgets::EndCard();
	Widgets::EndEntrance();
	if (!g_flow.active) {
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		return;
	}
	// The card's own height, not its (animated) position: cards rise into place.
	const float top = g_flow.fullWidth ? FlowBottom() : g_flow.columnY[g_flow.column];
	const float bottom = top + ImGui::GetItemRectSize().y + g_flow.gap;
	if (g_flow.fullWidth) {
		for (float& y : g_flow.columnY)
			y = bottom;
	} else {
		g_flow.columnY[g_flow.column] = bottom;
	}
	// Leave the cursor under every column, so whatever comes next starts below them.
	ImGui::SetCursorScreenPos(ImVec2(g_flow.x, FlowBottom()));
	ImGui::Dummy(ImVec2(0.0f, 0.0f));
	g_flow.afterCardY = ImGui::GetCursorScreenPos().y;
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

float g_labelWidth = 0.0f; // the open rows' label column

// Two-column label/control rows inside a card.
bool BeginRows(const char* id)
{
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(6.0f), S(9.0f)));
	if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
		ImGui::PopStyleVar();
		return false;
	}
	g_labelWidth = std::min(S(240.0f), std::floor(ImGui::GetContentRegionAvail().x * 0.4f));
	ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, g_labelWidth);
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
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + g_labelWidth - S(20.0f));
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

// A new custom theme, copied from `base`, with an unused id and name.
CustomTheme NewCustomTheme(const Settings& settings, const Theme::ThemeInfo& base)
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

// Editor for the selected custom theme. Edits apply live: main.cpp re-applies the
// style when the custom themes change.
bool DrawThemeEditor(Settings& settings)
{
	auto it = std::find_if(settings.customThemes.begin(), settings.customThemes.end(),
		[&](const CustomTheme& theme) { return theme.id == settings.theme; });
	if (it == settings.customThemes.end())
		return false;
	CustomTheme& theme = *it;
	bool changed = false;

	BeginCard("theme_editor", true);
	CardTitle("Edit your theme", "Changes apply as you make them and are saved automatically.");
	if (BeginRows("theme_editor_rows")) {
		Row("Name");
		changed |= ImGui::InputText("##theme_name", &theme.name);

		Row("Start from", "Copies another theme's colors into this one.");
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

		Row("Light theme", "Use with light backgrounds and dark text.");
		changed |= Widgets::Toggle("##theme_light", &theme.light);

		const auto& fields = Theme::PaletteFields();
		for (std::size_t i = 0; i < fields.size(); ++i) {
			Row(fields[i].label);
			ImGui::PushID(static_cast<int>(i));
			changed |= ImGui::ColorEdit3("##color", &theme.colors[i * 3],
				ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB);
			ImGui::PopID();
		}
		EndRows();
	}
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	if (ImGui::Button("Duplicate")) {
		const Theme::ThemeInfo copySource{ theme.id, theme.name, "", Theme::PaletteFromCustom(theme), true };
		CustomTheme copy = NewCustomTheme(settings, copySource);
		copy.name = theme.name + " copy";
		settings.customThemes.push_back(std::move(copy)); // invalidates `theme`
		settings.theme = settings.customThemes.back().id;
		EndCard();
		return true;
	}
	ImGui::SameLine();
	if (Widgets::DangerButton("Delete theme")) {
		settings.customThemes.erase(it);
		settings.theme = Theme::Themes()[0].id;
		changed = true;
	}
	EndCard();
	return changed;
}

// Settings > Game options: vanilla options.txt values applied to every pack.
bool DrawGameOptions(Settings& settings)
{
	bool changed = false;
	GameOptionsSettings& options = settings.gameOptions;

	Notice("Turn an option on to apply it to every pack when you press Play. Options left off keep "
		"whatever the pack or your in-game settings use.", Theme::C().info);

	BeginCard("game_options_mode");
	CardTitle("How they're applied");
	if (BeginRows("game_options_mode_rows")) {
		Row("Apply", "Every launch keeps all packs in sync but undoes in-game changes to these options.");
		int mode = options.everyLaunch ? 0 : 1;
		const char* modes[] = { "Every time a pack starts", "Only the first time each pack starts" };
		if (Widgets::Select("##apply_mode", &mode, modes, 2)) {
			options.everyLaunch = mode == 0;
			changed = true;
		}
		Row("Options turned on");
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%d", static_cast<int>(options.values.size()));
		ImGui::SameLine(0.0f, S(16.0f));
		ImGui::BeginDisabled(options.values.empty());
		if (ImGui::Button("Turn all off")) {
			options.values.clear();
			changed = true;
		}
		ImGui::EndDisabled();
		EndRows();
	}
	EndCard();

	const char* groups[] = { "Video", "Controls", "Sound", "Chat & accessibility", "First launch" };
	for (const char* group : groups) {
		ImGui::PushID(group);
		BeginCard("group");
		CardTitle(group);
		if (BeginRows("rows")) {
			for (const GameOptionDef& def : GameOptionDefs()) {
				if (std::string(def.group) != group)
					continue;
				ImGui::PushID(def.key);
				Row(def.label, def.hint);
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
			EndRows();
		}
		EndCard();
		ImGui::PopID();
	}
	return changed;
}

bool DrawAppearance(Settings& settings)
{
	bool changed = false;

	BeginCard("theme_card", true);
	CardTitle("Theme", "Pick a color theme, or create your own. It applies right away.");
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
				settings.customThemes.push_back(NewCustomTheme(settings, Theme::Themes()[current]));
				settings.theme = settings.customThemes.back().id;
				changed = true;
			}
		} else if (Widgets::ThemeCard(Theme::Themes()[i], i == current, cardWidth) && i != current) {
			settings.theme = Theme::Themes()[i].id;
			changed = true;
		}
	}
	EndCard();

	changed |= DrawThemeEditor(settings);

	BeginCard("display_card");
	CardTitle("Display");
	if (BeginRows("display_rows")) {
		Row("UI scale", "On top of your Windows display scaling. Also Ctrl +, Ctrl - and Ctrl 0.");
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

		Row("Reduce motion", "No page transitions, fade-ins, sliding notifications or smooth scrolling.");
		changed |= Widgets::Toggle("##reduce_motion", &settings.reduceMotion);

		Row("Gallery banners", "Show screenshots behind the selected pack. Turn off to save bandwidth.");
		changed |= Widgets::Toggle("##gallery", &settings.showGalleryBanners);
		EndRows();
	}
	EndCard();
	return changed;
}

// The Java choice: Automatic, one of the Javas found on this PC, or a custom path.
static bool DrawJavaChoice(LaunchSettings& launch, const SettingsPageContext& context)
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

// The Memory, Java and Game window cards. global adds the launcher-wide "When the game
// starts" choice; per-pack settings leave it out.
static bool DrawLaunchCards(LaunchSettings& launch, const SettingsPageContext& context, bool global)
{
	bool changed = false;
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
		Row("Java", "Automatic uses an installed Java with the version each Minecraft version asks for "
			"(for example 8 for 1.16, 17 for 1.18, 21 for 1.21), and downloads Mojang's when none matches.");
		changed |= DrawJavaChoice(launch, context);

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

		if (global) {
			Row("When the game starts");
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
		EndRows();
	}
	if (global) {
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		if (ImGui::Button("Reset Minecraft settings to defaults")) {
			launch = LaunchSettings{};
			changed = true;
		}
	}
	EndCard();
	return changed;
}

bool DrawMinecraft(Settings& settings, const SettingsPageContext& context)
{
	Notice(settings.packLaunch.empty() ? "These apply to every pack the next time you press Play."
		: "These apply to every pack the next time you press Play, except packs with their own launch settings "
		  "(My Library > Manage > Launch settings).", Theme::C().info);
	return DrawLaunchCards(settings.launch, context, true);
}

bool DrawPackLaunch(Settings& settings, const std::string& packId, const SettingsPageContext& context)
{
	bool changed = false;
	bool custom = settings.packLaunch.count(packId) > 0;
	BeginCard("pack_launch_card");
	if (BeginRows("pack_launch_rows")) {
		Row("Use custom launch settings", "Off uses Settings > Minecraft, like every other pack.");
		if (Widgets::Toggle("##pack_custom", &custom)) {
			if (custom)
				settings.packLaunch[packId] = settings.launch; // start from the global options
			else
				settings.packLaunch.erase(packId);
			changed = true;
		}
		EndRows();
	}
	EndCard();
	const auto it = settings.packLaunch.find(packId);
	if (it != settings.packLaunch.end())
		changed |= DrawLaunchCards(it->second, context, false);
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
	if (context.accountCount > 1)
		ImGui::TextDisabled("%d accounts saved. Switch, add or sign out from the ACCOUNT section of the sidebar.",
			context.accountCount);
	else
		ImGui::TextDisabled("Sign in, add another account, play offline, or sign out from the ACCOUNT section of the sidebar.");
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

	BeginCard("sharing_card");
	CardTitle("Shared mod files", "Mods and resource and shader packs that several packs use are kept once on "
		"disk, as hard links to one copy in minecraft/shared-files. Each pack still sees ordinary files: removing or "
		"turning off a mod in one pack doesn't touch the others. Explorer counts each link at full size, so folder "
		"sizes don't show the saving. Packs on another drive than the launcher get their own copies.");
	if (BeginRows("sharing_rows")) {
		Row("Share identical files", "Off: new installs get their own copies. Files already shared stay shared.");
		changed |= Widgets::Toggle("##share_mod_files", &settings.shareModFiles);
		Row("Packs already installed", "Finds identical mods in your packs and servers and keeps one copy of each. "
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
	Widgets::LogoBlock(S(64.0f));
	ImGui::SameLine(0.0f, S(16.0f));
	ImGui::BeginGroup();
	ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
	Widgets::Wordmark("MC LAUNCHER", std::floor(S(3.0f)), Theme::Mix(Theme::C().text, Theme::C().accent, 0.1f),
		Theme::Mix(Theme::C().accentDeep, Theme::C().windowBg, 0.15f));
	ImGui::TextDisabled("Premium edition");
	ImGui::EndGroup();
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
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

bool DrawPackLaunchSettings(Settings& settings, const std::string& packId, const SettingsPageContext& context)
{
	return DrawPackLaunch(settings, packId, context);
}

bool DrawSettingsPage(Settings& settings, const SettingsPageContext& context)
{
	g_cardsShownAt = std::max(g_cardsShownAt, context.shownAt);
	g_cardIndex = 0;
	TabStrip();
	ImGui::Spacing();

	bool changed = false;
	Widgets::BeginScrollArea("settings_scroll");
	BeginCardFlow();
	switch (g_tab) {
	case Tab::Appearance: changed = DrawAppearance(settings); break;
	case Tab::Minecraft: changed = DrawMinecraft(settings, context); break;
	case Tab::GameOptions: changed = DrawGameOptions(settings); break;
	case Tab::Account: changed = DrawAccount(settings, context); break;
	case Tab::Downloads: changed = DrawDownloads(settings, context); break;
	case Tab::About: DrawAbout(context); break;
	}
	EndCardFlow();
	Widgets::EndScrollArea();
	return changed;
}

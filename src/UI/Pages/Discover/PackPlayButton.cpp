#include "UI/Pages/Discover/PackPlayButton.h"

#include "Network/PackSources.h"
#include "UI/Theme.h"
#include "imgui.h"

#include <algorithm>

using Theme::S;

void PackPlayButton::Draw(const Modpack& pack, bool compact, float height)
{
	if (compact) {
		ImGui::PushStyleColor(ImGuiCol_Button, Theme::C().accentDeep);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Mix(Theme::C().accentDeep, Theme::C().accent, 0.35f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Mix(Theme::C().accentDeep, Theme::C().accent, 0.6f));
	}
	ImGui::PushFont(Theme::GetFonts().bold, Theme::kFontHeading);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
	Modpack* libraryPack = m_library.Find(pack.id);
	// Plays the version shown here when it's installed (making it the active one),
	// else the pack's active version.
	const InstalledVersion* shownInstalled = libraryPack && pack.filesLoaded
		? FindInstalledVersion(*libraryPack, VersionKey(pack)) : nullptr;
	const InstalledVersion* toPlay = shownInstalled ? shownInstalled
		: libraryPack ? ActiveInstalledVersion(*libraryPack) : nullptr;
	const bool playBusy = toPlay && m_plays.VersionBusy(pack.id, toPlay->key);
	// Greyed out until a version of the pack is installed.
	ImGui::BeginDisabled(!libraryPack || !toPlay || playBusy);
	// Spans the column, like the install card above it. The label names the version it launches.
	std::string label = playBusy ? "PLAYING" : "PLAY";
	if (toPlay && !toPlay->version.empty())
		label += " (" + toPlay->version + ")";
	label += "##play";
	if (ImGui::Button(label.c_str(), ImVec2(-1.0f, height > 0.0f ? height : S(compact ? 40.0f : 52.0f)))) {
		if (shownInstalled && shownInstalled->key != libraryPack->activeVersion) {
			const std::string key = shownInstalled->key;
			SelectInstalledVersion(*libraryPack, key);
			m_library.Save();
		}
		m_plays.Start(*libraryPack);
		m_navigator.ShowPage(Page::Library);
	}
	ImGui::EndDisabled();
	ImGui::PopStyleVar();
	ImGui::PopFont();
	if (compact)
		ImGui::PopStyleColor(3);
}

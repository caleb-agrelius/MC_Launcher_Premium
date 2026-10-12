#include "UI/Pages/Discover/PackPlayButton.h"

#include "Network/PackSources.h"
#include "UI/Theme.h"
#include "imgui.h"

using Theme::S;

void PackPlayButton::Draw(const Modpack& pack)
{
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
	ImGui::BeginDisabled(!libraryPack || playBusy);
	if (ImGui::Button(playBusy ? "PLAYING" : "PLAY", ImVec2(-1.0f, S(52.0f)))) {
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
	if (!libraryPack)
		ImGui::TextDisabled(GetSourceCapabilities(pack.source).installsPacks
			? "Install the pack to play it." : "Install the pack's file to play it.");
	else if (!shownInstalled && pack.filesLoaded)
		ImGui::TextDisabled("This version isn't installed. PLAY starts your installed %s.",
			libraryPack->version.c_str());
	else
		ImGui::TextDisabled("Progress and errors appear in My Library.");
}

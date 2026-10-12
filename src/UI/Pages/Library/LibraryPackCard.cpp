#include "UI/Pages/Library/LibraryPackCard.h"

#include "Game/InstanceContent.h"
#include "Network/PackSources.h"
#include "Platform/Platform.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Play/PlayService.h"
#include "Services/Servers/ServerService.h"
#include "Services/Updates/UpdateService.h"
#include "UI/Common/UiHelpers.h"
#include "UI/ImageCache.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include <set>
#include <string>
#include <vector>

using Theme::S;

void LibraryPackCard::Draw(Modpack& pack)
{
	const Theme::Fonts& fonts = Theme::GetFonts();

	// The card shows the active version's play state; other versions can be running too.
	const PlayState play = m_plays.StateOf(m_plays.Key(pack));
	std::vector<std::string> otherRunning; // version names
	std::set<std::string> runningKeys;
	for (const InstalledVersion& v : pack.installedVersions) {
		const PlayState other = m_plays.StateOf(pack.id + "@" + v.key);
		if (!(other.busy || other.playing))
			continue;
		runningKeys.insert(v.key);
		if (v.key != pack.activeVersion)
			otherRunning.push_back(v.version.empty() ? v.key : v.version);
	}

	Widgets::BeginCard(pack.id.c_str());
	UiHelpers::PackIconItem(pack, m_images, S(56.0f));
	ImGui::SameLine(0.0f, S(14.0f));
	ImGui::BeginGroup();
	const bool hostable = m_servers.CanHost(pack);
	const PackUpdate* runningUpdate = m_updates.Running(pack.id);
	const bool updating = runningUpdate != nullptr;
	ImGui::PushTextWrapPos(0.0f);
	Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::C().text, pack.name.c_str());

	// With several installed versions, a dropdown picks the active one (what PLAY,
	// Open folder and Manage act on).
	const bool multipleVersions = pack.installedVersions.size() > 1;
	std::string details = PackSourceName(pack.source);
	if (!multipleVersions)
		details += "  \xC2\xB7  " + pack.version;
	if (!pack.gameVersion.empty())
		details += "  \xC2\xB7  Minecraft " + pack.gameVersion;
	if (!pack.loaders.empty())
		details += "  \xC2\xB7  " + pack.loaders;
	std::string switchTo;
	if (multipleVersions) {
		ImGui::SetNextItemWidth(S(190.0f));
		ImGui::BeginDisabled(updating);
		if (Widgets::BeginSelect("##installed_version", pack.version.c_str())) {
			for (const InstalledVersion& v : pack.installedVersions) {
				ImGui::PushID(v.key.c_str());
				std::string minecraft = v.gameVersion.empty() ? std::string() : "Minecraft " + v.gameVersion;
				if (!v.contentChanges.empty())
					minecraft += minecraft.empty() ? "modified" : ", modified";
				if (runningKeys.count(v.key))
					minecraft += minecraft.empty() ? "running" : ", running";
				if (Widgets::ListItem(v.version.empty() ? v.key.c_str() : v.version.c_str(), v.key == pack.activeVersion,
						true, minecraft.c_str()))
					switchTo = v.key;
				ImGui::PopID();
			}
			Widgets::EndSelect();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%d versions installed. PLAY starts the one picked here; each has its own "
			"mods, settings and worlds, and several can run at once. Install more from Discover.",
			static_cast<int>(pack.installedVersions.size()));
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
	}
	ImGui::TextDisabled("%s", details.c_str());
	if (const InstalledVersion* active = ActiveInstalledVersion(pack); active && !active->contentChanges.empty()) {
		ImGui::SameLine();
		Widgets::ModifiedChip(DescribeContentChanges(active->contentChanges));
	}
	if (!pack.description.empty())
		Widgets::WrappedText(Theme::C().textDim, ShortenText(pack.description, 160).c_str());
	if (!switchTo.empty()) {
		SelectInstalledVersion(pack, switchTo);
		m_library.Save();
	}
	if (!otherRunning.empty()) {
		std::string names;
		for (std::size_t i = 0; i < otherRunning.size(); ++i)
			names += (i ? ", " : "") + otherRunning[i];
		ImGui::TextColored(Theme::C().accent, "Also running: %s", names.c_str());
		ImGui::SetItemTooltip("Pick it in the version list to see its progress or stop it.");
	}
	if (play.playing) {
		ImGui::TextColored(Theme::C().accent, "Minecraft is running");
	} else if (updating) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled(runningUpdate->copying ? "Copying your worlds and settings to %s"
			: "Updating to %s (progress is on the Downloads page)", runningUpdate->toVersion.c_str());
	} else if (play.busy) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", play.step.c_str());
	} else if (!play.error.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().error);
		ImGui::TextWrapped("%s", play.error.c_str());
		ImGui::PopStyleColor();
	} else {
		ImGui::TextDisabled("Ready to play");
	}
	ImGui::EndGroup();
	ImGui::PopTextWrapPos();

	// Actions, right-aligned as one group so nothing runs past the card's edge.
	ImGui::PushFont(fonts.bold, 0.0f);
	if (play.playing) {
		UiHelpers::AlignRightForButtons({ "Stop game" });
		if (Widgets::DangerButton("Stop game"))
			m_dialogs.ConfirmStopGame(m_plays.Key(pack));
		ImGui::SetItemTooltip("Force-close Minecraft");
	} else if (play.busy) {
		const bool cancelling = play.cancel->load();
		UiHelpers::AlignRightForButtons({ cancelling ? "Cancelling..." : "Cancel" });
		ImGui::BeginDisabled(cancelling);
		if (ImGui::Button(cancelling ? "Cancelling..." : "Cancel"))
			m_plays.Cancel(m_plays.Key(pack));
		ImGui::EndDisabled();
	} else {
		UiHelpers::AlignRightForButtons(hostable ? std::vector<const char*>{ "Server", "Open folder", "Manage", "  PLAY  " }
			: std::vector<const char*>{ "Open folder", "Manage", "  PLAY  " });
		if (hostable) {
			if (ImGui::Button("Server"))
				m_navigator.ShowServer(pack.id);
			ImGui::SetItemTooltip("Host this pack as a server for your friends");
			ImGui::SameLine();
		}
		if (ImGui::Button("Open folder"))
			Platform::OpenFolder(m_library.GameDir(pack).string());
		ImGui::SetItemTooltip("Open the game folder (mods, config, saves, logs)");
		ImGui::SameLine();
		if (ImGui::Button("Manage"))
			m_navigator.ShowManage(pack.id);
		ImGui::SetItemTooltip("Content, changelog, logs, screenshots, versions and launch settings");
		ImGui::SameLine();
		ImGui::BeginDisabled(updating);
		if (Widgets::PrimaryButton("  PLAY  "))
			m_plays.Start(pack);
		ImGui::EndDisabled();
	}
	ImGui::PopFont();
	if (play.busy && !play.playing && play.progress >= 0.0)
		Widgets::ThinProgressBar(static_cast<float>(play.progress));
	Widgets::EndCard();
}

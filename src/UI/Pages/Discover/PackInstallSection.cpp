#include "UI/Pages/Discover/PackInstallSection.h"

#include "Game/InstanceContent.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"
#include "imgui.h"

#include <cstdio>

using Theme::S;
using Widgets::BeginCard;
using Widgets::EndCard;
using UiHelpers::AlignRightForButton;

void PackInstallSection::Draw(SourceView& view, Modpack& pack, bool compact)
{
	const bool online = pack.source != PackSource::LocalIndex;
	if (view.caps.installsPacks && online)
		DrawPlanInstallCard(view, pack, compact);
	else
		DrawFileCards(view, pack, compact);
}

void PackInstallSection::DrawPlanInstallCard(SourceView& view, Modpack& pack, bool compact)
{
	// Whole-pack install: one card for the chosen version. Versions install side by side.
	if (!compact)
		Widgets::SectionLabel("INSTALL");
	const std::string key = "pack:" + pack.id + "@" + VersionKey(pack);
	DownloadState state;
	const bool hasState = m_downloads.Find(key, state);
	const bool loadingFiles = m_details.FilesLoading(pack.id);
	const std::string* filesError = m_details.FilesError(pack.id);
	const Modpack* libraryEntry = m_library.Find(pack.id);
	const InstalledVersion* installedHere = libraryEntry ? FindInstalledVersion(*libraryEntry, VersionKey(pack)) : nullptr;
	const bool inLibrary = installedHere != nullptr;
	const std::string versionFolder = installedHere ? installedHere->gameDir
		: PlanInstalledVersion(m_library.Packs(), pack, true).gameDir;

	BeginCard("install_card");
	ImGui::AlignTextToFramePadding();
	if (loadingFiles) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Loading latest version...");
	} else if (filesError) {
		ImGui::TextColored(Theme::C().error, "Couldn't load versions: %s", filesError->c_str());
		AlignRightForButton("Retry");
		if (ImGui::Button("Retry"))
			m_details.FetchFiles(view, pack);
	} else if (pack.filesLoaded) {
		// Version to install; the newest release is chosen at first.
		ImGui::SetNextItemWidth(S(200.0f));
		ImGui::BeginDisabled(hasState && state.active);
		PackVersionInfo picked;
		if (m_versions.Combo("##install_version", pack, pack.version.empty() ? "Latest version" : pack.version,
				pack.sourceVersionId, picked, libraryEntry))
			VersionPicker::Apply(pack, picked);
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Pick which version of the pack to install");
		ImGui::SameLine();
		std::string details;
		if (!pack.gameVersion.empty())
			details = "Minecraft " + pack.gameVersion;
		if (!pack.loaders.empty())
			details += (details.empty() ? "" : "  \xC2\xB7  ") + pack.loaders;
		ImGui::TextDisabled("%s", details.c_str());

		if (hasState && state.active) {
			std::string progressLabel = state.stage;
			if (state.stageTotal > 0)
				progressLabel += "  " + std::to_string(state.stageDone) + " / " + std::to_string(state.stageTotal);
			AlignRightForButton(progressLabel.c_str());
			ImGui::TextDisabled("%s", progressLabel.c_str());
		} else {
			const bool installed = (hasState && state.installed) || inLibrary;
			const char* label = installed ? "Reinstall" : (hasState && !state.error.empty() ? "Retry" : "Install pack");
			AlignRightForButton(label);
			if (ImGui::Button(label))
				m_installer.RequestInstall(pack);
		}
	}
	if (hasState && state.active) {
		if (state.stageTotal > 0)
			Widgets::ThinProgressBar(static_cast<float>(static_cast<double>(state.stageDone) / static_cast<double>(state.stageTotal)));
		else
			Widgets::ThinProgressBar(0.0f);
	}
	if (hasState && !state.error.empty())
		ImGui::TextColored(Theme::C().error, "Install failed: %s", state.error.c_str());
	if (!(hasState && state.active) && ((hasState && state.installed) || inLibrary)) {
		ImGui::TextColored(Theme::C().accent, "Installed");
		ImGui::SameLine();
		ImGui::TextDisabled("to %s", versionFolder.c_str());
		if (installedHere && !installedHere->contentChanges.empty()) {
			ImGui::SameLine();
			Widgets::ModifiedChip(DescribeContentChanges(installedHere->contentChanges));
		}
	}
	if (hasState && !state.blocked.empty()) {
		ImGui::Spacing();
		ImGui::TextColored(Theme::C().error, "%llu file(s) must be downloaded by hand into %s:",
			static_cast<unsigned long long>(state.blocked.size()), versionFolder.c_str());
		for (const PlannedFile& file : state.blocked) {
			ImGui::PushID(file.relPath.c_str());
			ImGui::BulletText("%s  ->  %s", file.name.c_str(), file.relPath.c_str());
			if (!file.manualPageUrl.empty()) {
				ImGui::SameLine();
				ImGui::TextLinkOpenURL("download page", file.manualPageUrl.c_str());
			}
			if (!file.blockedReason.empty())
				ImGui::TextDisabled("    %s", file.blockedReason.c_str());
			ImGui::PopID();
		}
	}
	for (const std::string& note : state.notes)
		ImGui::TextDisabled("Note: %s", note.c_str());
	EndCard();
}

void PackInstallSection::DrawVersionRow(Modpack& pack, float comboWidth)
{
	// Which version's .mrpack to download; the newest release is chosen at first.
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("Version");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(S(comboWidth));
	PackVersionInfo picked;
	if (m_versions.Combo("##file_version", pack, pack.version.empty() ? "Latest version" : pack.version,
			pack.sourceVersionId, picked, m_library.Find(pack.id)))
		VersionPicker::Apply(pack, picked);
	ImGui::SameLine();
	std::string details = pack.gameVersion.empty() ? std::string() : "Minecraft " + pack.gameVersion;
	if (!pack.loaders.empty())
		details += (details.empty() ? "" : "  \xC2\xB7  ") + pack.loaders;
	ImGui::TextDisabled("%s", details.c_str());
}

void PackInstallSection::DrawFileCards(SourceView& view, Modpack& pack, bool compact)
{
	const bool online = pack.source != PackSource::LocalIndex;
	const bool loadingFiles = m_details.FilesLoading(pack.id);
	const std::string* filesError = m_details.FilesError(pack.id);
	// Compact with a single file: the version picker moves into that file's card.
	const bool merged = compact && online && pack.filesLoaded && pack.files.size() == 1;
	if (!compact) {
		char filesLabel[48];
		std::snprintf(filesLabel, sizeof(filesLabel), "PACK FILES  (%llu)", static_cast<unsigned long long>(pack.files.size()));
		Widgets::SectionLabel(filesLabel);
	}
	if (loadingFiles) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Loading latest version...");
	} else if (filesError) {
		ImGui::TextColored(Theme::C().error, "Couldn't load versions: %s", filesError->c_str());
		if (ImGui::Button("Retry"))
			m_details.FetchFiles(view, pack);
	}
	if (online && pack.filesLoaded && !merged)
		DrawVersionRow(pack, 220.0f);

	for (const PackFile& file : pack.files) {
		ImGui::PushID(file.url.c_str());
		BeginCard("file_card");
		if (merged)
			DrawVersionRow(pack, 150.0f);
		DrawFileRow(pack, file);
		EndCard();
		ImGui::PopID();
	}
}

void PackInstallSection::DrawFileRow(Modpack& pack, const PackFile& file)
{
	const Theme::Fonts& fonts = Theme::GetFonts();
	DownloadState state;
	const bool hasState = m_downloads.Find(file.url, state);

	ImGui::AlignTextToFramePadding();
	ImGui::PushFont(fonts.bold, 0.0f);
	ImGui::TextUnformatted(file.name.c_str());
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", FormatFileSize(file.size).c_str());

	if (!hasState || (!state.active && state.outPath.empty())) {
		// One step: download, then extract into installs/<pack id>/ right away.
		const char* label = hasState ? "Retry" : "Install";
		AlignRightForButton(label);
		if (ImGui::Button(label))
			m_downloads.StartDownloadInstall(pack, file);
		if (hasState && !state.error.empty())
			ImGui::TextColored(Theme::C().error, "Download failed: %s", state.error.c_str());
	} else if (state.active) {
		char progressLabel[64];
		if (state.total > 0.0)
			std::snprintf(progressLabel, sizeof(progressLabel), "Downloading  %.0f%%", 100.0 * state.now / state.total);
		else
			std::snprintf(progressLabel, sizeof(progressLabel), "Downloading");
		AlignRightForButton(progressLabel);
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", progressLabel);
	} else if (state.installing) {
		AlignRightForButton("Extracting");
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Extracting");
	} else if (state.installed) {
		// The installed version, and whether the player changed it.
		const Modpack* entry = m_library.Find(pack.id);
		const InstalledVersion* here = entry ? FindInstalledVersion(*entry, VersionKey(pack)) : nullptr;
		const bool modified = here && !here->contentChanges.empty();
		AlignRightForButton(modified ? "Installed    Modified" : "Installed");
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(Theme::C().accent, "Installed");
		if (modified) {
			ImGui::SameLine();
			Widgets::ModifiedChip(DescribeContentChanges(here->contentChanges));
		}
	} else if (!state.outPath.empty()) {
		// Downloaded earlier but not extracted (an older session, or extraction failed).
		AlignRightForButton("Install");
		if (ImGui::Button("Install"))
			m_downloads.StartInstall(file.url, pack, state.outPath);
	}

	if (state.active && state.total > 0.0)
		Widgets::ThinProgressBar(static_cast<float>(state.now / state.total));
	else if (state.installing)
		Widgets::ThinProgressBar(static_cast<float>(state.installProgress));
	if (!state.installError.empty())
		ImGui::TextColored(Theme::C().error, "Extraction failed: %s", state.installError.c_str());
}

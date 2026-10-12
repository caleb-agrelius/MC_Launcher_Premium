#include "UI/Pages/Discover/PackInstallSection.h"

#include "Game/InstanceContent.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using Theme::S;
using Widgets::BeginCard;
using Widgets::EndCard;
using UiHelpers::AlignRightForButton;

constexpr float kMinDetailsScale = 0.65f;

// The row's dim details ("Minecraft 1.20.1", the loader, a file size, most important first) joined with dots. They
// shrink (to 65% at most) to fit the room left on the row, less `reserve` for what follows; if they still don't
// fit, the least important are left out rather than run into it.
static void FittedDetails(std::vector<std::string> parts, float reserve)
{
	auto join = [&parts] {
		std::string text;
		for (const std::string& part : parts)
			text += (text.empty() ? "" : "  \xC2\xB7  ") + part;
		return text;
	};
	const float room = ImGui::GetContentRegionAvail().x - reserve;
	std::string text = join();
	while (parts.size() > 1 && ImGui::CalcTextSize(text.c_str()).x * kMinDetailsScale > room) {
		parts.pop_back();
		text = join();
	}
	const float width = ImGui::CalcTextSize(text.c_str()).x;
	const float scale = width > room && width > 0.0f ? std::max(kMinDetailsScale, room / width) : 1.0f;
	if (scale >= 1.0f) {
		ImGui::TextDisabled("%s", text.c_str());
		return;
	}
	// Smaller text hangs from the top of the line, so it is drawn centered on the line the full-size text would
	// take, and the row keeps that height.
	ImGui::AlignTextToFramePadding();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float lineHeight = ImGui::GetTextLineHeight();
	ImGui::PushFont(nullptr, Theme::kFontBody * scale); // the size is unscaled: ImGui applies the UI scale itself
	const float smallHeight = ImGui::GetTextLineHeight();
	const float smallWidth = ImGui::CalcTextSize(text.c_str()).x;
	ImGui::GetWindowDrawList()->AddText(
		ImVec2(pos.x, pos.y + ImGui::GetStyle().FramePadding.y + (lineHeight - smallHeight) * 0.5f),
		ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(smallWidth, lineHeight));
}

// "1.2 GB" (or "~1.2 GB" when the real total is larger) for the version's download, empty until known.
static std::string SizeText(const PackDetailsService& details, const Modpack& pack)
{
	const PackSizeEstimate* size = details.Size(PackDetailsService::ModsKey(pack));
	if (!size || size->bytes == 0)
		return std::string();
	return (size->partial ? "~" : "") + FormatFileSize(size->bytes);
}

static const char* const kSizeTooltip =
	"How much this version's files download (the game, loader and Java are separate). "
	"\"~\" means at least that much: some sizes aren't known.";

std::string PackInstallSection::Title(const SourceView& view, const Modpack& pack)
{
	if (view.caps.installsPacks && pack.source != PackSource::LocalIndex)
		return "INSTALL";
	char title[48];
	std::snprintf(title, sizeof(title), "PACK FILES  (%llu)", static_cast<unsigned long long>(pack.files.size()));
	return title;
}

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
	// Also while the fetch is about to start, so the card never shows up empty first.
	const bool loadingFiles = m_details.FilesLoading(pack.id) || (!pack.filesLoaded && !m_details.FilesError(pack.id));
	const std::string* filesError = m_details.FilesError(pack.id);
	const Modpack* libraryEntry = m_library.Find(pack.id);
	const InstalledVersion* installedHere = libraryEntry ? FindInstalledVersion(*libraryEntry, VersionKey(pack)) : nullptr;
	const bool inLibrary = installedHere != nullptr;
	const std::string versionFolder = installedHere ? installedHere->gameDir
		: PlanInstalledVersion(m_library.Packs(), pack, true).gameDir;

	BeginCard("install_card");
	ImGui::AlignTextToFramePadding();
	if (loadingFiles) {
		// The same row as the loaded card: version picker, its details and the Install button.
		const float rowHeight = ImGui::GetFrameHeight();
		Widgets::LoadingField("Loading versions...", S(200.0f));
		ImGui::SameLine();
		Widgets::Skeleton(ImVec2(S(150.0f), rowHeight), 4.0f, S(12.0f));
		AlignRightForButton("Install pack");
		Widgets::Skeleton(ImVec2(Widgets::ButtonWidth("Install pack"), rowHeight));
	} else if (filesError) {
		ImGui::TextColored(Theme::C().error, "Couldn't load versions: %s", filesError->c_str());
		AlignRightForButton("Retry");
		if (ImGui::Button("Retry"))
			m_details.FetchFiles(view, pack);
	} else if (pack.filesLoaded) {
		// Version to install; the newest release is chosen at first.
		ImGui::SetNextItemWidth(std::min(S(200.0f), ImGui::GetContentRegionAvail().x * 0.45f));
		ImGui::BeginDisabled(hasState && state.active);
		PackVersionInfo picked;
		if (m_versions.Combo("##install_version", pack, pack.version.empty() ? "Latest version" : pack.version,
				pack.sourceVersionId, picked, libraryEntry))
			VersionPicker::Apply(pack, picked);
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Pick which version of the pack to install");
		ImGui::SameLine();
		std::vector<std::string> details;
		if (!pack.gameVersion.empty())
			details.push_back("Minecraft " + pack.gameVersion);
		const std::string sizeText = SizeText(m_details, pack);
		if (!sizeText.empty())
			details.push_back(sizeText);
		if (!pack.loaders.empty())
			details.push_back(pack.loaders);
		std::string progressLabel = state.stage;
		if (state.stageTotal > 0)
			progressLabel += "  " + std::to_string(state.stageDone) + " / " + std::to_string(state.stageTotal);
		// Leave room for whatever sits at the right: the progress text, or the largest button label.
		const float reserve = std::max({ ImGui::CalcTextSize(progressLabel.c_str()).x, Widgets::ButtonWidth("Reinstall"),
			Widgets::ButtonWidth("Install pack") }) + ImGui::GetStyle().ItemSpacing.x;
		FittedDetails(details, reserve);
		if (!sizeText.empty())
			ImGui::SetItemTooltip("%s", kSizeTooltip);

		if (hasState && state.active) {
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

void PackInstallSection::DrawVersionRowPlaceholder(float comboWidth)
{
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("Version");
	ImGui::SameLine();
	Widgets::LoadingField("Loading...", S(comboWidth));
	ImGui::SameLine();
	Widgets::Skeleton(ImVec2(S(130.0f), ImGui::GetFrameHeight()), 4.0f, S(12.0f));
}

void PackInstallSection::DrawFileRowPlaceholder()
{
	const float rowHeight = ImGui::GetFrameHeight();
	Widgets::Skeleton(ImVec2(S(210.0f), rowHeight), 4.0f, S(14.0f));
	ImGui::SameLine();
	Widgets::Skeleton(ImVec2(S(50.0f), rowHeight), 4.0f, S(12.0f));
	AlignRightForButton("Install");
	Widgets::Skeleton(ImVec2(Widgets::ButtonWidth("Install"), rowHeight));
}

void PackInstallSection::DrawVersionRow(Modpack& pack, float comboWidth, bool withLabel, float reserve, const PackFile* file)
{
	// Which version's .mrpack to download; the newest release is chosen at first.
	ImGui::AlignTextToFramePadding();
	if (withLabel) {
		ImGui::TextDisabled("Version");
		ImGui::SameLine();
	}
	ImGui::SetNextItemWidth(std::min(S(comboWidth), ImGui::GetContentRegionAvail().x * 0.45f));
	PackVersionInfo picked;
	if (m_versions.Combo("##file_version", pack, pack.version.empty() ? "Latest version" : pack.version,
			pack.sourceVersionId, picked, m_library.Find(pack.id)))
		VersionPicker::Apply(pack, picked);
	ImGui::SameLine();
	std::vector<std::string> details;
	if (!pack.gameVersion.empty())
		details.push_back("Minecraft " + pack.gameVersion);
	const std::string sizeText = SizeText(m_details, pack);
	if (!sizeText.empty())
		details.push_back(sizeText);
	if (!pack.loaders.empty())
		details.push_back(pack.loaders);
	FittedDetails(details, reserve);
	if (file)
		ImGui::SetItemTooltip("%s\n%s", file->name.c_str(), kSizeTooltip);
}

void PackInstallSection::DrawFileCards(SourceView& view, Modpack& pack, bool compact)
{
	const bool online = pack.source != PackSource::LocalIndex;
	const bool loadingFiles = m_details.FilesLoading(pack.id) || (online && !pack.filesLoaded && !m_details.FilesError(pack.id));
	const std::string* filesError = m_details.FilesError(pack.id);
	// Compact with a single file: the version picker moves into that file's card.
	const bool merged = compact && online && pack.filesLoaded && pack.files.size() == 1;
	if (!compact)
		Widgets::SectionLabel(Title(view, pack).c_str());
	if (loadingFiles && online) {
		// What the loaded section will hold: one file, with its version picker (in the card when compact).
		if (!compact)
			DrawVersionRowPlaceholder(220.0f);
		BeginCard("file_card");
		if (compact) {
			// One row, like the whole-pack card: version picker, its details and the Install button.
			const float rowHeight = ImGui::GetFrameHeight();
			ImGui::AlignTextToFramePadding();
			Widgets::LoadingField("Loading versions...", S(200.0f));
			ImGui::SameLine();
			Widgets::Skeleton(ImVec2(S(150.0f), rowHeight), 4.0f, S(12.0f));
			AlignRightForButton("Install");
			Widgets::Skeleton(ImVec2(Widgets::ButtonWidth("Install"), rowHeight));
		} else {
			DrawFileRowPlaceholder();
		}
		EndCard();
		return;
	}
	if (filesError) {
		ImGui::TextColored(Theme::C().error, "Couldn't load versions: %s", filesError->c_str());
		if (ImGui::Button("Retry"))
			m_details.FetchFiles(view, pack);
	}
	if (online && pack.filesLoaded && !merged)
		DrawVersionRow(pack, 220.0f, true, 0.0f, nullptr);

	for (const PackFile& file : pack.files) {
		ImGui::PushID(file.url.c_str());
		BeginCard("file_card");
		// Merged: one row, like the whole-pack card. The version picker leads, then the version's details
		// with the file's size (its name is the tooltip), then Install.
		if (merged)
			DrawVersionRow(pack, 200.0f, false,
				std::max(Widgets::ButtonWidth("Installed    Modified"), Widgets::ButtonWidth("Reinstall")) + ImGui::GetStyle().ItemSpacing.x, &file);
		DrawFileRow(pack, file, merged);
		EndCard();
		ImGui::PopID();
	}
}

void PackInstallSection::DrawFileRow(Modpack& pack, const PackFile& file, bool inlineRow)
{
	const Theme::Fonts& fonts = Theme::GetFonts();
	DownloadState state;
	const bool hasState = m_downloads.Find(file.url, state);

	// In the one-row layout the state stays on the version row's line (the details text has shrunk to leave room).
	auto alignRight = [inlineRow](const char* label) {
		if (inlineRow)
			ImGui::SameLine(ImGui::GetContentRegionMax().x - Widgets::ButtonWidth(label));
		else
			AlignRightForButton(label);
	};
	ImGui::AlignTextToFramePadding();
	if (!inlineRow) {
		ImGui::PushFont(fonts.bold, 0.0f);
		ImGui::TextUnformatted(file.name.c_str());
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::TextDisabled("%s", FormatFileSize(file.size).c_str());
	}

	if (!hasState || (!state.active && state.outPath.empty())) {
		// One step: download, then extract into installs/<pack id>/ right away.
		const char* label = hasState ? "Retry" : "Install";
		alignRight(label);
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
		alignRight(progressLabel);
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", progressLabel);
	} else if (state.installing) {
		alignRight("Extracting");
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Extracting");
	} else if (state.installed) {
		// The installed version, and whether the player changed it.
		const Modpack* entry = m_library.Find(pack.id);
		const InstalledVersion* here = entry ? FindInstalledVersion(*entry, VersionKey(pack)) : nullptr;
		const bool modified = here && !here->contentChanges.empty();
		alignRight(modified ? "Installed    Modified" : "Installed");
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(Theme::C().accent, "Installed");
		if (modified) {
			ImGui::SameLine();
			Widgets::ModifiedChip(DescribeContentChanges(here->contentChanges));
		}
	} else if (!state.outPath.empty()) {
		// Downloaded earlier but not extracted (an older session, or extraction failed).
		alignRight("Install");
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

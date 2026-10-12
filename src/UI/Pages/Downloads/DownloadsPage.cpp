#include "UI/Pages/Downloads/DownloadsPage.h"

#include "Services/Downloads/DownloadService.h"
#include "UI/Common/UiHelpers.h"
#include "UI/ImageCache.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include <cstdint>

using Theme::S;

void DownloadsPage::DrawHeader()
{
	UiHelpers::DrawPageHeader("MINECRAFT  /  DOWNLOADS", "Downloads", "Track downloads and extraction progress.");
}

void DownloadsPage::Draw(const PageFrame& frame)
{
	const auto snapshot = m_downloads.Snapshot();
	if (snapshot.empty() && Widgets::EmptyState("No downloads yet",
			"Pick a modpack on Discover and press Install.", "Browse modpacks"))
		m_navigator.ShowPage(Page::Discover);
	int cardIndex = 0;
	for (const auto& item : snapshot) {
		Widgets::BeginEntrance(frame.shownAt, cardIndex++);
		DrawCard(item.first, item.second);
		Widgets::EndEntrance();
	}
}

void DownloadsPage::DrawCard(const std::string& key, const DownloadState& state)
{
	const Theme::Fonts& fonts = Theme::GetFonts();
	Widgets::BeginCard(key.c_str());
	UiHelpers::PackIconItem(state.pack, m_images, S(56.0f));
	ImGui::SameLine(0.0f, S(14.0f));
	ImGui::BeginGroup();
	ImGui::PushTextWrapPos(0.0f);
	Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::C().text,
		state.pack.name.empty() ? state.fileName.c_str() : state.pack.name.c_str());
	if (!state.pack.name.empty())
		ImGui::TextDisabled("%s", state.fileName.c_str());
	if (state.planInstall) {
		if (state.active) {
			if (state.stageTotal > 0)
				ImGui::TextDisabled("%s...  %llu of %llu", state.stage.c_str(),
					static_cast<unsigned long long>(state.stageDone), static_cast<unsigned long long>(state.stageTotal));
			else
				ImGui::TextDisabled("%s...", state.stage.c_str());
		} else if (!state.error.empty()) {
			ImGui::TextColored(Theme::C().error, "Install failed: %s", state.error.c_str());
		} else if (!state.blocked.empty()) {
			ImGui::TextColored(Theme::C().error, "Installed - %llu file(s) need a manual download (see Discover)",
				static_cast<unsigned long long>(state.blocked.size()));
		} else {
			ImGui::TextColored(Theme::C().accent, "Installed");
		}
	} else if (state.active) {
		if (state.total > 0.0)
			ImGui::TextDisabled("Downloading...  %s of %s", FormatFileSize(static_cast<std::uint64_t>(state.now)).c_str(),
				FormatFileSize(static_cast<std::uint64_t>(state.total)).c_str());
		else
			ImGui::TextDisabled("Downloading...");
	} else if (!state.error.empty()) {
		ImGui::TextColored(Theme::C().error, "Download failed: %s", state.error.c_str());
	} else if (state.installing) {
		ImGui::TextDisabled("Extracting files...");
	} else if (!state.installError.empty()) {
		ImGui::TextColored(Theme::C().error, "Extraction failed: %s", state.installError.c_str());
	} else if (state.installed) {
		ImGui::TextColored(Theme::C().accent, "Files extracted");
	} else {
		ImGui::TextDisabled("Downloaded - ready to extract.");
	}
	ImGui::EndGroup();
	ImGui::PopTextWrapPos();
	if (!state.active && !state.outPath.empty() && !state.installing && !state.installed) {
		UiHelpers::AlignRightForButton("Install files");
		if (ImGui::Button("Install files"))
			m_downloads.StartInstall(key, state.pack, state.outPath);
	}
	if (state.planInstall && state.active)
		Widgets::ThinProgressBar(state.stageTotal > 0
			? static_cast<float>(static_cast<double>(state.stageDone) / static_cast<double>(state.stageTotal)) : 0.0f);
	else if (state.active && state.total > 0.0)
		Widgets::ThinProgressBar(static_cast<float>(state.now / state.total));
	else if (state.installing)
		Widgets::ThinProgressBar(static_cast<float>(state.installProgress));
	Widgets::EndCard();
}

#include "UI/Pages/Discover/DiscoverDetails.h"

#include "UI/ImageCache.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"
#include "imgui.h"

#include <algorithm>
#include <cstring>

using Theme::S;

// The downloads chip and the pack's page link on one line; trimScheme drops "https://" from the link text.
void DiscoverDetails::DrawStatsRow(const Modpack& pack, bool trimScheme)
{
	if (pack.downloads > 0) {
		ImGui::AlignTextToFramePadding(); // the chip centers on the link's line
		Widgets::Chip((FormatCount(pack.downloads) + " downloads").c_str(), Theme::C().accent);
		ImGui::SameLine();
	}
	if (pack.websiteUrl.empty())
		return;
	std::string shown = pack.websiteUrl;
	for (const char* scheme : { "https://", "http://" }) {
		if (trimScheme && shown.rfind(scheme, 0) == 0)
			shown.erase(0, std::strlen(scheme));
	}
	ImGui::AlignTextToFramePadding();
	ImGui::TextLinkOpenURL(shown.c_str(), pack.websiteUrl.c_str());
}

// The banner on the left, with the version picker, install section and PLAY in a column beside it.
void DiscoverDetails::DrawCompactTop(SourceView& view, Modpack& pack)
{
	const float rowHeight = S(190.0f);
	const float sideWidth = std::min(S(440.0f), ImGui::GetContentRegionAvail().x * 0.5f);
	const float gap = ImGui::GetStyle().ItemSpacing.x;
	if (ImGui::BeginChild("##details_banner", ImVec2(ImGui::GetContentRegionAvail().x - sideWidth - gap, rowHeight),
			ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		Widgets::HeroBanner(pack, m_images, rowHeight, m_settings.Get().showGalleryBanners);
	ImGui::EndChild();
	ImGui::SameLine();
	if (ImGui::BeginChild("##details_side", ImVec2(0.0f, rowHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar)) {
		m_install.Draw(view, pack, true);
		ImGui::Spacing();
		m_play.Draw(pack, true);
	}
	ImGui::EndChild();
}

// The list view's install card on the left (75% of the width) and PLAY on the right (25%), the same
// height as the card and level with it. Draws the section label above both.
void DiscoverDetails::DrawInstallAndPlay(SourceView& view, Modpack& pack)
{
	Widgets::SectionLabel(PackInstallSection::Title(view, pack).c_str());
	const float gap = ImGui::GetStyle().ItemSpacing.x;
	const float usable = std::max(0.0f, ImGui::GetContentRegionAvail().x - gap);
	// The compact layout: no labels, and a pack with one file gets a single one-row card.
	if (ImGui::BeginChild("##details_install", ImVec2(usable * 0.75f, 0.0f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar))
		m_install.Draw(view, pack, true);
	ImGui::EndChild();
	const float height = ImGui::GetItemRectSize().y;
	ImGui::SameLine(0.0f, gap);
	if (ImGui::BeginChild("##details_play", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar))
		m_play.Draw(pack, false, height);
	ImGui::EndChild();
}

void DiscoverDetails::Draw(SourceView* view, Modpack* selected, bool searching, const PageFrame& frame, bool compact)
{
	Widgets::BeginScrollArea("pack_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	const std::string shownId = selected ? selected->id : std::string();
	if (shownId != m_shownPackId) {
		m_shownPackId = shownId;
		m_shownAt = ImGui::GetTime();
		m_keepBottom = 0.0f;
	}
	Widgets::BeginEntrance(std::max(frame.shownAt, m_shownAt));
	if (selected) {
		const Settings& settings = m_settings.Get();
		Modpack& pack = *selected;
		const bool online = pack.source != PackSource::LocalIndex;
		if (online && !pack.filesLoaded && !m_details.FilesLoading(pack.id) && !m_details.FilesError(pack.id))
			m_details.FetchFiles(*view, pack);
		// How much the version downloads, for the install row; once per version.
		if (online && pack.filesLoaded && CanEstimatePackSize(pack)) {
			const std::string sizeKey = PackDetailsService::ModsKey(pack);
			if (!m_details.Size(sizeKey) && !m_details.SizeLoading(sizeKey) && !m_details.SizeFailed(sizeKey))
				m_details.FetchSize(pack);
		}
		if (settings.showGalleryBanners && pack.source == PackSource::Modrinth && !pack.bannerLoaded && !m_details.BannerLoading(pack.id))
			m_details.FetchBanner(*view, pack.id);

		if (compact) {
			DrawCompactTop(*view, pack);
		} else {
			Widgets::HeroBanner(pack, m_images, std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, S(200.0f), S(340.0f)),
				settings.showGalleryBanners);
			ImGui::Spacing();
		}
		if (online)
			DrawStatsRow(pack, !compact);
		ImGui::Spacing();
		if (!compact) { // the compact layout leaves the description to its tab
			ImGui::PushFont(nullptr, Theme::kFontBody + 1.0f);
			ImGui::TextWrapped("%s", pack.description.c_str());
			ImGui::PopFont();
			ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
				DrawInstallAndPlay(*view, pack);
			ImGui::Dummy(ImVec2(0.0f, S(10.0f))); // what's in the pack
		}
		if (online && m_tabs.Draw(pack))
			m_keepBottom = ImGui::GetScrollY() + ImGui::GetWindowHeight();
		// Keep the height the view had when the tab changed (see m_keepBottom).
		if (ImGui::GetCursorPosY() < m_keepBottom)
			ImGui::Dummy(ImVec2(0.0f, m_keepBottom - ImGui::GetCursorPosY()));
	} else if (!searching) {
		ImGui::TextDisabled("No modpacks match your search.");
	}
	Widgets::EndEntrance();
	Widgets::EndScrollArea();
}

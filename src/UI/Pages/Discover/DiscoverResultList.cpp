#include "UI/Pages/Discover/DiscoverResultList.h"

#include "UI/Common/UiHelpers.h"
#include "UI/ImageCache.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"
#include "imgui.h"

#include <algorithm>

using Theme::S;

std::vector<std::string> DiscoverResultList::DrawStatus(const DiscoverPlan& plan)
{
	DiscoverState& discover = m_discover.State();
	auto& views = m_discover.Views();
	const float rowTop = ImGui::GetCursorPosY();
	std::vector<std::string> searchingNames;
	for (PackSource source : discover.activeSources) {
		if (views[source].searching)
			searchingNames.push_back(PackSourceName(source));
	}
	if (!searchingNames.empty()) {
		std::string names;
		for (std::size_t i = 0; i < searchingNames.size(); ++i)
			names += (i ? ", " : "") + searchingNames[i];
		ImGui::AlignTextToFramePadding(); // as tall as the results line that replaces it, so the list doesn't drop
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Searching %s...", names.c_str());
		Widgets::PadToHeight(rowTop, ImGui::GetFrameHeight()); // the height of the results line that replaces it
	} else if (discover.activeSources.empty()) {
		Widgets::WrappedText(Theme::C().error, plan.skipped.empty() ? "Pick at least one source."
			: ("No source can run this search. " + plan.skipped.front()).c_str());
	} else {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%zu results", discover.merged.size());
		for (PackSource source : discover.activeSources) {
			const SourceView& view = views[source];
			ImGui::SameLine(0.0f, S(14.0f));
			if (view.searchFailed) {
				ImGui::TextColored(Theme::C().error, "%s failed", PackSourceName(source));
				ImGui::SetItemTooltip("%s", view.searchStatus.c_str());
			} else {
				ImGui::TextColored(Theme::Mix(UiHelpers::SourceColor(source), Theme::C().text, 0.3f), "%s", PackSourceName(source));
				ImGui::SameLine(0.0f, S(5.0f));
				ImGui::TextDisabled("%s", FormatCount(view.totalHits).c_str());
			}
		}
		if (!discover.skippedSources.empty()) {
			std::string why;
			for (const std::string& line : discover.skippedSources)
				why += (why.empty() ? "" : "\n") + line;
			ImGui::SameLine(0.0f, S(14.0f));
			ImGui::TextDisabled("(%zu skipped)", discover.skippedSources.size());
			ImGui::SetItemTooltip("%s", why.c_str());
		}
	}
	return searchingNames;
}

void DiscoverResultList::DrawList(const Modpack* selected, bool searching, double pageShownAt)
{
	DiscoverState& discover = m_discover.State();
	auto& views = m_discover.Views();
	const bool singleResult = discover.activeSources.size() == 1;
	const bool browsing = discover.search[0] == '\0' && discover.gameVersion[0] == '\0' &&
		discover.loader.empty() && discover.sort == 0;
	const float discoverAvail = ImGui::GetContentRegionAvail().x / S(1.0f);
	const bool grid = discover.viewMode == DiscoverViewMode::Grid;
	// The grid fills the page; the details of a clicked card open in a modal (DiscoverPage).
	float listWidth = m_settings.PaneWidth("discover", 340.0f, discoverAvail - 420.0f);
	Widgets::BeginScrollArea("pack_list", ImVec2(grid ? 0.0f : S(listWidth), 0.0f), ImGuiChildFlags_Borders);
	double rowsShownAt = pageShownAt;
	for (PackSource source : discover.activeSources)
		rowsShownAt = std::max(rowsShownAt, views[source].resultsAt);
	Widgets::SectionLabel(browsing ? (singleResult ? views[discover.activeSources.front()].caps.browseLabel : "POPULAR") : "RESULTS");
	if (discover.viewMode == DiscoverViewMode::Grid)
		DrawGrid(selected, rowsShownAt, singleResult);
	else
		DrawRows(selected, rowsShownAt, singleResult);
	if (discover.merged.empty() && !searching && !discover.activeSources.empty())
		ImGui::TextDisabled("No modpacks found.");
	if (!searching && !discover.merged.empty())
		DrawLoadMore();
	Widgets::EndScrollArea();
	if (grid)
		return;
	ImGui::SameLine(0.0f, 0.0f);
	if (Widgets::PaneSplitter("##discover_split", &listWidth, 240.0f, discoverAvail - 420.0f))
		m_settings.SetPaneWidth("discover", listWidth);
	ImGui::SameLine(0.0f, 0.0f);
}

DiscoverResultList::ResultInfo DiscoverResultList::Describe(const Modpack& item, bool singleResult) const
{
	ResultInfo info;
	info.pack = &item;
	if (item.source == PackSource::LocalIndex) {
		info.meta = item.version + "  \xC2\xB7  " + std::to_string(item.files.size()) + " files";
	} else {
		info.meta = item.gameVersion;
		if (item.downloads > 0)
			info.meta += (info.meta.empty() ? "" : "  \xC2\xB7  ") + FormatCount(item.downloads) + " downloads";
		if (info.meta.empty())
			info.meta = item.author;
	}
	if (!singleResult || item.source == PackSource::LocalIndex)
		info.tag = item.source == PackSource::LocalIndex ? "Offline" : PackSourceName(item.source);
	return info;
}

void DiscoverResultList::DrawRows(const Modpack* selected, double rowsShownAt, bool singleResult)
{
	DiscoverState& discover = m_discover.State();
	auto& views = m_discover.Views();
	for (std::size_t i = 0; i < discover.merged.size(); ++i) {
		const auto& ref = discover.merged[i];
		const ResultInfo info = Describe(views[ref.first].packs[ref.second], singleResult);
		ImGui::PushID(static_cast<int>(i));
		Widgets::BeginEntrance(rowsShownAt, static_cast<int>(i));
		if (Widgets::PackRow(*info.pack, m_images, selected == info.pack, info.meta, info.tag, UiHelpers::SourceColor(info.pack->source)))
			discover.selectedId = info.pack->id;
		Widgets::EndEntrance();
		ImGui::PopID();
	}
}

void DiscoverResultList::DrawGrid(const Modpack* selected, double rowsShownAt, bool singleResult)
{
	DiscoverState& discover = m_discover.State();
	auto& views = m_discover.Views();
	const float gap = ImGui::GetStyle().ItemSpacing.x;
	const float avail = ImGui::GetContentRegionAvail().x;
	const int columns = std::max(1, static_cast<int>((avail + gap) / (S(190.0f) * static_cast<float>(discover.gridScalePercent) / 100.0f + gap)));
	const float cardWidth = (avail - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
	for (std::size_t i = 0; i < discover.merged.size(); ++i) {
		const auto& ref = discover.merged[i];
		const ResultInfo info = Describe(views[ref.first].packs[ref.second], singleResult);
		if (i % static_cast<std::size_t>(columns) != 0)
			ImGui::SameLine();
		ImGui::PushID(static_cast<int>(i));
		Widgets::BeginEntrance(rowsShownAt, static_cast<int>(i));
		if (Widgets::PackCard(*info.pack, m_images, selected == info.pack, info.meta, info.tag,
				UiHelpers::SourceColor(info.pack->source), cardWidth)) {
			discover.selectedId = info.pack->id;
			discover.detailsOpen = true;
		}
		Widgets::EndEntrance();
		ImGui::PopID();
	}
}

void DiscoverResultList::DrawLoadMore()
{
	DiscoverState& discover = m_discover.State();
	auto& views = m_discover.Views();
	bool more = false;
	bool loading = false;
	std::string pageError;
	for (PackSource source : discover.activeSources) {
		const SourceView& view = views[source];
		more = more || view.HasMore();
		loading = loading || view.loadingMore;
		if (!view.loadMoreError.empty() && pageError.empty())
			pageError = std::string(PackSourceName(source)) + ": " + view.loadMoreError;
	}
	const auto loadNextPage = [&] {
		for (PackSource source : discover.activeSources) {
			if (views[source].HasMore())
				m_discover.StartSearch(views[source], true);
		}
	};
	ImGui::Spacing();
	if (loading) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Loading more...");
	} else if (more && pageError.empty()) {
		// Scrolling near the bottom loads the next page.
		if (ImGui::GetScrollMaxY() - ImGui::GetScrollY() < S(300.0f))
			loadNextPage();
	} else if (more) {
		Widgets::WrappedText(Theme::C().error, ("Couldn't load more: " + pageError).c_str());
		if (ImGui::Button("Retry"))
			loadNextPage();
	}
}

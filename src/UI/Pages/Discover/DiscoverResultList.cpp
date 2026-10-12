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
	std::vector<std::string> searchingNames;
	for (PackSource source : discover.activeSources) {
		if (views[source].searching)
			searchingNames.push_back(PackSourceName(source));
	}
	if (!searchingNames.empty()) {
		std::string names;
		for (std::size_t i = 0; i < searchingNames.size(); ++i)
			names += (i ? ", " : "") + searchingNames[i];
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Searching %s...", names.c_str());
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
	float listWidth = m_settings.PaneWidth("discover", 340.0f, discoverAvail - 420.0f);
	Widgets::BeginScrollArea("pack_list", ImVec2(S(listWidth), 0.0f), ImGuiChildFlags_Borders);
	double rowsShownAt = pageShownAt;
	for (PackSource source : discover.activeSources)
		rowsShownAt = std::max(rowsShownAt, views[source].resultsAt);
	Widgets::SectionLabel(browsing ? (singleResult ? views[discover.activeSources.front()].caps.browseLabel : "POPULAR") : "RESULTS");
	for (std::size_t i = 0; i < discover.merged.size(); ++i) {
		const auto& ref = discover.merged[i];
		const Modpack& item = views[ref.first].packs[ref.second];
		std::string meta;
		if (item.source == PackSource::LocalIndex) {
			meta = item.version + "  \xC2\xB7  " + std::to_string(item.files.size()) + " files";
		} else {
			meta = item.gameVersion;
			if (item.downloads > 0)
				meta += (meta.empty() ? "" : "  \xC2\xB7  ") + FormatCount(item.downloads) + " downloads";
			if (meta.empty())
				meta = item.author;
		}
		ImGui::PushID(static_cast<int>(i));
		Widgets::BeginEntrance(rowsShownAt, static_cast<int>(i));
		const bool showTag = !singleResult || item.source == PackSource::LocalIndex;
		if (Widgets::PackRow(item, m_images, selected == &item, meta,
				showTag ? (item.source == PackSource::LocalIndex ? "Offline" : PackSourceName(item.source)) : nullptr,
				UiHelpers::SourceColor(item.source)))
			discover.selectedId = item.id;
		Widgets::EndEntrance();
		ImGui::PopID();
	}
	if (discover.merged.empty() && !searching && !discover.activeSources.empty())
		ImGui::TextDisabled("No modpacks found.");
	// Next page from every source that has more.
	if (!searching && !discover.merged.empty()) {
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
		ImGui::Spacing();
		if (loading) {
			Widgets::Spinner(Theme::C().accent);
			ImGui::SameLine();
			ImGui::TextDisabled("Loading more...");
		} else if (more) {
			if (ImGui::Button("Load more", ImVec2(-1.0f, 0.0f))) {
				for (PackSource source : discover.activeSources) {
					if (views[source].HasMore())
						m_discover.StartSearch(views[source], true);
				}
			}
			if (!pageError.empty())
				Widgets::WrappedText(Theme::C().error, ("Couldn't load more: " + pageError).c_str());
		}
	}
	Widgets::EndScrollArea();
	ImGui::SameLine(0.0f, 0.0f);
	if (Widgets::PaneSplitter("##discover_split", &listWidth, 240.0f, discoverAvail - 420.0f))
		m_settings.SetPaneWidth("discover", listWidth);
	ImGui::SameLine(0.0f, 0.0f);
}

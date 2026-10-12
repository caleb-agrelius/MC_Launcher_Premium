#include "UI/Pages/Discover/DiscoverFilterBar.h"

#include "UI/Common/UiHelpers.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "imgui.h"

#include <string>

using Theme::S;

namespace {

const char* const kCombinedSorts[] = { "Best match", "Most downloads", "Name" };

}

void DiscoverFilterBar::Draw(const DiscoverPlan& plan)
{
	DiscoverState& discover = m_discover.State();
	auto& views = m_discover.Views();
	const bool single = plan.active.size() == 1;
	const SourceCapabilities* singleCaps = single ? &views[plan.active.front()].caps : nullptr;
	const int sortCount = single ? static_cast<int>(singleCaps->sorts.size()) : 3;
	if (discover.sort >= sortCount)
		discover.sort = 0;

	// Search box.
	ImGui::SetNextItemWidth(-1.0f);
	if (m_focusSearch) {
		ImGui::SetKeyboardFocusHere();
		m_focusSearch = false;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14.0f), S(11.0f)));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
	const char* hint = single ? singleCaps->searchHint : "Search modpacks on every source...";
	if (ImGui::InputTextWithHint("##search", hint, discover.search, sizeof(discover.search))) {
		discover.searchPending = true;
		discover.lastSearchEdit = ImGui::GetTime();
	}
	ImGui::PopStyleVar(2);

	// Source filter chips.
	bool filtersChanged = false; // chips and combos search right away; text waits for typing to pause
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12.0f), S(5.0f))); // the chips' padding
	ImGui::AlignTextToFramePadding();
	ImGui::PopStyleVar();
	ImGui::TextDisabled("Sources");
	ImGui::SameLine();
	if (Widgets::ToggleChip("All", m_discover.AllSourcesSelected(), Theme::C().accent)) {
		m_discover.ToggleAllSources();
		filtersChanged = true;
	}
	ImGui::SetItemTooltip(m_discover.AllSourcesSelected() ? "Deselect every source" : "Select every source");
	for (PackSource source : OnlineSources()) {
		std::string reason;
		const bool available = IsSourceAvailable(source, &reason);
		const bool on = discover.sources.count(source) > 0;
		ImGui::SameLine();
		if (Widgets::ToggleChip(PackSourceName(source), on && available, UiHelpers::SourceColor(source), available)) {
			m_discover.ToggleSource(source);
			filtersChanged = true;
		}
		if (!available)
			ImGui::SetItemTooltip("%s", reason.c_str());
	}

	// View switch: list or grid, on the right of the source chips.
	const float buttonSize = ImGui::GetFrameHeight();
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	ImGui::SameLine(ImGui::GetContentRegionMax().x - 2.0f * buttonSize - spacing);
	if (Widgets::ViewModeButton("##view_list", false, discover.viewMode == DiscoverViewMode::List))
		discover.viewMode = DiscoverViewMode::List;
	ImGui::SetItemTooltip("List view");
	ImGui::SameLine();
	if (Widgets::ViewModeButton("##view_grid", true, discover.viewMode == DiscoverViewMode::Grid))
		discover.viewMode = DiscoverViewMode::Grid;
	ImGui::SetItemTooltip("Grid view");

	// Minecraft version, loader, sort.
	ImGui::SetNextItemWidth(S(170.0f));
	if (ImGui::InputTextWithHint("##mcversion", "Minecraft version", discover.gameVersion, sizeof(discover.gameVersion))) {
		discover.searchPending = true;
		discover.lastSearchEdit = ImGui::GetTime();
	}
	ImGui::SetItemTooltip("Only packs for this Minecraft version, e.g. 1.20.1");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(S(150.0f));
	if (Widgets::BeginSelect("##loader", discover.loader.empty() ? "Any loader" : UiHelpers::LoaderLabel(discover.loader.c_str()))) {
		if (Widgets::ListItem("Any loader", discover.loader.empty())) {
			discover.loader.clear();
			filtersChanged = true;
		}
		for (const char* key : UiHelpers::kLoaderKeys) {
			if (Widgets::ListItem(UiHelpers::LoaderLabel(key), discover.loader == key)) {
				discover.loader = key;
				filtersChanged = true;
			}
		}
		Widgets::EndSelect();
	}
	ImGui::SameLine();
	DrawCategoryPicker(filtersChanged);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(S(210.0f));
	const char* sortLabel = single ? singleCaps->sorts[static_cast<std::size_t>(discover.sort)] : kCombinedSorts[discover.sort];
	if (Widgets::BeginSelect("##sort", (std::string("Sort: ") + sortLabel).c_str())) {
		for (int i = 0; i < sortCount; ++i) {
			if (Widgets::ListItem(single ? singleCaps->sorts[static_cast<std::size_t>(i)] : kCombinedSorts[i], discover.sort == i)) {
				discover.sort = i;
				filtersChanged = true;
			}
		}
		Widgets::EndSelect();
	}
	if (!single)
		ImGui::SetItemTooltip("Best match mixes each source's top results. Pick one source for its own sort options.");
	if (filtersChanged) {
		discover.searchPending = true;
		discover.lastSearchEdit = -1.0; // no typing delay
		m_discover.SaveSourceFilter();
	}
}

void DiscoverFilterBar::DrawCategoryPicker(bool& filtersChanged)
{
	DiscoverState& discover = m_discover.State();
	std::string label = "Any category";
	for (const ModrinthCategory& category : discover.categories) {
		if (category.key == discover.category)
			label = category.label;
	}
	ImGui::SetNextItemWidth(S(170.0f));
	if (Widgets::BeginSelect("##category", label.c_str())) {
		m_discover.LoadCategories(); // the first time the menu opens
		if (Widgets::ListItem("Any category", discover.category.empty())) {
			discover.category.clear();
			filtersChanged = true;
		}
		if (discover.categoriesLoading) {
			UiHelpers::BusyLabel("Loading categories...");
		} else if (!discover.categoriesError.empty()) {
			ImGui::TextColored(Theme::C().error, "Couldn't load categories");
			if (Widgets::ListItem("Retry", false))
				discover.categoriesError.clear();
		}
		for (const ModrinthCategory& category : discover.categories) {
			if (Widgets::ListItem(category.label.c_str(), discover.category == category.key)) {
				discover.category = category.key;
				filtersChanged = true;
			}
		}
		Widgets::EndSelect();
	}
	ImGui::SetItemTooltip("Modrinth categories, e.g. Adventure or Technology. Other sources can't filter by category, "
		"so they're skipped while one is chosen.");
}

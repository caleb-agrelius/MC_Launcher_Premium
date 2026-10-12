#include "UI/Pages/Discover/DiscoverPage.h"

#include "UI/Common/UiHelpers.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "imgui.h"

#include <algorithm>

void DiscoverPage::DrawHeader()
{
	UiHelpers::DrawPageHeader("MINECRAFT  /  MODPACKS", "Discover modpacks",
		"Search Modrinth, CurseForge, FTB, Technic and ATLauncher at once.");
}

void DiscoverPage::Draw(const PageFrame& frame)
{
	m_filters.Draw(m_discover.Plan());
	// Planned again, so a filter changed this frame is already in the search it sends.
	const DiscoverPlan plan = m_discover.Plan();
	m_discover.SendIfDue(plan, plan.active.size() == 1);
	m_discover.Remerge();
	const std::vector<std::string> searching = m_results.DrawStatus(plan);

	PackSource source = PackSource::Modrinth;
	Modpack* selected = m_discover.Selected(source);
	m_results.DrawList(selected, !searching.empty(), frame.shownAt);
	SourceView* view = selected ? &m_discover.Views()[source] : nullptr;
	DiscoverState& state = m_discover.State();
	if (state.viewMode == DiscoverViewMode::List) {
		state.detailsOpen = false;
		m_details.Draw(view, selected, !searching.empty(), frame);
		return;
	}
	DrawDetailsModal(view, selected, !searching.empty(), frame);
}

// Grid view: the selected pack's details in a modal over the page, closed with the X or Escape.
void DiscoverPage::DrawDetailsModal(SourceView* view, Modpack* selected, bool searching, const PageFrame& frame)
{
	DiscoverState& state = m_discover.State();
	if (!state.detailsOpen || !selected)
		return;
	if (!ImGui::IsPopupOpen("Modpack details"))
		ImGui::OpenPopup("Modpack details");
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowSize(ImVec2(display.x * 0.8f, display.y * 0.86f)); // follows the window
	ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	bool open = true;
	if (ImGui::BeginPopupModal("Modpack details", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
		if (Widgets::ModalCloseButton())
			open = false;
		m_details.Draw(view, selected, searching, frame, true);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			ImGui::CloseCurrentPopup(); // the scrolling child inside takes the focus, so Escape needs asking for
		ImGui::EndPopup();
	}
	if (!open || !ImGui::IsPopupOpen("Modpack details"))
		state.detailsOpen = false;
}

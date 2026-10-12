#include "UI/Pages/Discover/DiscoverPage.h"

#include "UI/Common/UiHelpers.h"

void DiscoverPage::DrawHeader()
{
	UiHelpers::DrawPageHeader("MINECRAFT  /  MODPACKS", "Discover modpacks",
		"Search Modrinth, CurseForge, FTB, Technic and ATLauncher at once.");
}

void DiscoverPage::Draw(const PageFrame& frame)
{
	const DiscoverPlan plan = m_discover.Plan();
	const bool single = plan.active.size() == 1;
	m_filters.Draw(plan);
	m_discover.SendIfDue(plan, single);
	m_discover.Remerge();
	const std::vector<std::string> searching = m_results.DrawStatus(plan);

	PackSource source = PackSource::Modrinth;
	Modpack* selected = m_discover.Selected(source);
	m_results.DrawList(selected, !searching.empty(), frame.shownAt);
	m_details.Draw(selected ? &m_discover.Views()[source] : nullptr, selected, !searching.empty(), frame);
}

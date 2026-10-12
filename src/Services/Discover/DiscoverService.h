#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Services/Discover/DiscoverModel.h"
#include "Services/Settings/SettingsStore.h"

#include <functional>
#include <map>
#include <vector>

// Discover's combined search across the online sources: which sources a search goes to, sending
// it, paging, and merging the sources' results into one list. UI thread only.
class DiscoverService {
public:
	// now() is the UI clock, in seconds, for debounce and result timestamps.
	using Clock = std::function<double()>;

	// localPacks is the offline fallback list shown when Modrinth can't be reached.
	DiscoverService(const Runtime& runtime, SettingsStore& settings, std::vector<Modpack> localPacks, Clock now);

	// One view per online source. std::map keeps references stable, so background tasks can hold a SourceView&.
	std::map<PackSource, SourceView>& Views() { return m_views; }
	DiscoverState& State() { return m_state; }

	// Which sources the source filter selects, minus sources that can't be used and, when a loader
	// or category is chosen, sources that can't filter by it.
	DiscoverPlan Plan();

	// Fetches the Modrinth category list for the category picker, once.
	void LoadCategories();

	// Remembers the source filter in settings.
	void SaveSourceFilter();

	// Sends the pending search to every active source once typing has paused.
	void SendIfDue(const DiscoverPlan& plan, bool singleSource);
	// Merges the sources' results whenever one of them changed.
	void Remerge();
	// Runs a search for one source. With append, fetches the next page (starting after the
	// results already loaded) and adds it to them, for "Load more".
	void StartSearch(SourceView& view, bool append = false);

	// The selected pack (kept across re-merges, else the first result) and the source it came from.
	Modpack* Selected(PackSource& source);

private:
	Runtime m_rt;
	SettingsStore& m_settings;
	std::vector<Modpack> m_localPacks;
	Clock m_now;
	std::map<PackSource, SourceView> m_views;
	DiscoverState m_state;
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};

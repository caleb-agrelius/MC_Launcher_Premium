#include "Services/Discover/DiscoverService.h"

#include "Util/Text.h"

#include <algorithm>
#include <cstdio>
#include <utility>

DiscoverService::DiscoverService(const Runtime& runtime, SettingsStore& settings, std::vector<Modpack> localPacks, Clock now,
	ICacheControl& cache, PackDetailsService& details)
	: m_rt(runtime), m_settings(settings), m_localPacks(std::move(localPacks)), m_now(std::move(now)), m_cache(cache),
	  m_details(details), m_scope(runtime.tasks)
{
	for (PackSource source : OnlineSources()) {
		SourceView& view = m_views[source];
		view.source = source;
		view.caps = GetSourceCapabilities(source);
	}
	for (const std::string& key : settings.Get().discoverSources) {
		const PackSource source = PackSourceFromKey(key);
		if (m_views.count(source))
			m_state.sources.insert(source);
	}
	if (m_state.sources.empty())
		m_state.sources.insert(OnlineSources().begin(), OnlineSources().end()); // nothing saved = every source
	m_state.gridScalePercent = settings.Get().discoverGridScale;
}

DiscoverPlan DiscoverService::Plan()
{
	DiscoverPlan plan;
	for (PackSource source : OnlineSources()) {
		if (m_state.sources.count(source) == 0)
			continue;
		std::string reason;
		if (!IsSourceAvailable(source, &reason)) {
			plan.skipped.push_back(std::string(PackSourceName(source)) + ": " + reason);
			continue;
		}
		if (!m_state.loader.empty() && !m_views[source].caps.loaderFilter) {
			plan.skipped.push_back(std::string(PackSourceName(source)) + " can't filter by loader");
			continue;
		}
		if (!m_state.category.empty() && !m_views[source].caps.categoryFilter) {
			plan.skipped.push_back(std::string(PackSourceName(source)) + " can't filter by category");
			continue;
		}
		plan.active.push_back(source);
	}
	return plan;
}

void DiscoverService::LoadCategories()
{
	if (!m_state.categories.empty() || m_state.categoriesLoading || !m_state.categoriesError.empty())
		return;
	m_state.categoriesLoading = true;
	m_scope.Run([this]() {
		std::vector<ModrinthCategory> categories;
		std::string error;
		const bool ok = FetchModrinthCategories(m_rt.http, categories, &error, &m_rt.shuttingDown);
		m_rt.ui.Post([this, ok, categories, error]() {
			m_state.categoriesLoading = false;
			m_state.categories = categories;
			m_state.categoriesError = ok ? std::string() : error;
		});
	});
}

void DiscoverService::Refresh()
{
	m_cache.Clear();
	ClearSourceCaches(); // FTB and ATLauncher also keep their whole pack list for the run
	m_details.Clear();
	const bool hadCategories = !m_state.categories.empty();
	m_state.categories.clear();
	m_state.categoriesError.clear();
	if (hadCategories)
		LoadCategories();
	m_state.searchPending = true; // SendIfDue sends the current search on the next frame
	m_state.lastSearchEdit = -1.0;
	m_rt.log << "Discover refresh: cache cleared, searching again\n";
}

bool DiscoverService::Searching() const
{
	if (m_state.searchPending)
		return true;
	for (const auto& entry : m_views) {
		if (entry.second.searching)
			return true;
	}
	return false;
}

bool DiscoverService::AllSourcesSelected() const
{
	return m_state.sources.size() == OnlineSources().size();
}

void DiscoverService::ToggleSource(PackSource source)
{
	if (!m_state.sources.erase(source))
		m_state.sources.insert(source);
}

void DiscoverService::ToggleAllSources()
{
	const bool deselect = AllSourcesSelected();
	m_state.sources.clear();
	if (!deselect)
		m_state.sources.insert(OnlineSources().begin(), OnlineSources().end());
}

void DiscoverService::SaveSourceFilter()
{
	Settings& settings = m_settings.Get();
	settings.discoverSources.clear();
	for (PackSource source : m_state.sources)
		settings.discoverSources.push_back(PackSourceKey(source));
	m_settings.MarkDirty();
}

void DiscoverService::SetGridScale(int percent)
{
	percent = std::clamp(percent, kMinGridScale, kMaxGridScale);
	if (percent == m_state.gridScalePercent)
		return;
	m_state.gridScalePercent = percent;
	m_settings.Get().discoverGridScale = percent;
	m_settings.MarkDirty();
}

void DiscoverService::SendIfDue(const DiscoverPlan& plan, bool singleSource)
{
	DiscoverState& discover = m_state;
	if (!discover.searchPending || m_now() - discover.lastSearchEdit < kSearchDelaySeconds)
		return;
	discover.searchPending = false;
	discover.activeSources = plan.active;
	discover.skippedSources = plan.skipped;
	for (PackSource source : plan.active) {
		SourceView& view = m_views[source];
		std::snprintf(view.search, sizeof(view.search), "%s", discover.search);
		std::snprintf(view.gameVersion, sizeof(view.gameVersion), "%s", discover.gameVersion);
		view.loader = 0;
		for (std::size_t i = 0; i < view.caps.loaders.size(); ++i) {
			if (discover.loader == view.caps.loaders[i])
				view.loader = static_cast<int>(i + 1);
		}
		view.sort = singleSource ? discover.sort : 0;
		view.category = view.caps.categoryFilter ? discover.category : std::string();
		StartSearch(view);
	}
	discover.mergedKey.clear();
}

void DiscoverService::Remerge()
{
	DiscoverState& discover = m_state;
	const bool singleResult = discover.activeSources.size() == 1;
	std::string mergeKey = std::to_string(discover.sort);
	for (PackSource source : discover.activeSources)
		mergeKey += "," + std::to_string(m_views[source].revision);
	if (mergeKey == discover.mergedKey)
		return;
	discover.mergedKey = mergeKey;
	discover.merged.clear();
	std::size_t longest = 0;
	for (PackSource source : discover.activeSources)
		longest = std::max(longest, m_views[source].packs.size());
	for (std::size_t rank = 0; rank < longest; ++rank) {
		for (PackSource source : discover.activeSources) {
			if (rank < m_views[source].packs.size())
				discover.merged.emplace_back(source, rank);
		}
	}
	if (!singleResult && discover.sort != 0) {
		auto packOf = [&](const std::pair<PackSource, std::size_t>& ref) -> const Modpack& {
			return m_views[ref.first].packs[ref.second];
		};
		std::stable_sort(discover.merged.begin(), discover.merged.end(), [&](const auto& a, const auto& b) {
			if (discover.sort == 1)
				return packOf(a).downloads > packOf(b).downloads;
			return ToLower(packOf(a).name) < ToLower(packOf(b).name);
		});
	}
}

Modpack* DiscoverService::Selected(PackSource& source)
{
	DiscoverState& discover = m_state;
	for (const auto& ref : discover.merged) {
		Modpack& item = m_views[ref.first].packs[ref.second];
		if (item.id == discover.selectedId) {
			source = ref.first;
			return &item;
		}
	}
	if (!discover.merged.empty()) {
		source = discover.merged.front().first;
		Modpack* pack = &m_views[source].packs[discover.merged.front().second];
		discover.selectedId = pack->id;
		return pack;
	}
	return nullptr;
}

void DiscoverService::StartSearch(SourceView& view, bool append)
{
	// A new search makes older results (and pages still loading) stale; a page doesn't.
	const int generation = append ? view.searchGeneration : ++view.searchGeneration;
	const std::string query = view.search;
	SearchFilters filters;
	filters.gameVersion = view.gameVersion;
	if (view.loader > 0 && view.loader <= static_cast<int>(view.caps.loaders.size()))
		filters.loader = view.caps.loaders[static_cast<std::size_t>(view.loader - 1)];
	filters.sort = view.sort;
	filters.category = view.category;
	filters.offset = append ? static_cast<int>(view.packs.size()) : 0;
	const int limit = m_settings.Get().searchLimit;
	if (append) {
		view.loadingMore = true;
		view.loadMoreError.clear();
	} else {
		view.searching = true;
		view.loadingMore = false;
		view.loadMoreError.clear();
	}
	m_scope.Run([this, &view, query, filters, limit, generation, append]() {
		SourceSearchResult result;
		std::string error;
		const bool ok = SearchPacks(m_rt.http, view.source, query, filters, limit, result, &error, &m_rt.shuttingDown);
		m_rt.ui.Post([this, &view, query, filters, generation, append, ok, result, error]() {
			if (generation != view.searchGeneration)
				return;
			if (append) {
				view.loadingMore = false;
				if (!ok) {
					view.loadMoreError = error;
					m_rt.log << PackSourceName(view.source) << " page at " << filters.offset << " failed: " << error << "\n";
					return;
				}
				// Results can shift between pages (e.g. a pack's rank changes), so skip repeats.
				std::set<std::string> seen;
				for (const Modpack& pack : view.packs)
					seen.insert(pack.id);
				std::size_t added = 0;
				for (const Modpack& pack : result.packs) {
					if (seen.insert(pack.id).second) {
						view.packs.push_back(pack);
						++added;
					}
				}
				// An empty page means the source has nothing more, whatever its total said.
				view.totalHits = result.packs.empty() ? view.packs.size() : std::max<std::uint64_t>(result.totalHits, view.packs.size());
				++view.revision;
				m_rt.log << PackSourceName(view.source) << " page at " << filters.offset << ": " << added << " more\n";
				return;
			}
			const char* sourceName = PackSourceName(view.source);
			view.searching = false;
			view.searchFailed = !ok;
			view.totalHits = ok ? result.totalHits : 0;
			++view.revision;
			if (ok) {
				view.packs = result.packs;
				view.resultsAt = m_now();
				view.searchStatus = FormatCount(result.totalHits) + " modpacks on " + sourceName;
				m_rt.log << sourceName << " search \"" << query << "\": " << result.packs.size() << " of "
					<< result.totalHits << " result(s)\n";
			} else {
				m_rt.log << sourceName << " search \"" << query << "\" failed: " << error << "\n";
				view.packs.clear();
				if (view.source == PackSource::Modrinth) {
					// Offline or API error: fall back to the local index.
					const std::string lowerQuery = ToLower(query);
					for (const Modpack& pack : m_localPacks) {
						if (lowerQuery.empty() || ContainsIgnoreCase(pack.name, lowerQuery) ||
							ContainsIgnoreCase(pack.description, lowerQuery))
							view.packs.push_back(pack);
					}
					view.searchStatus = "Modrinth unavailable (" + error + ") - showing local packs";
				} else {
					view.searchStatus = std::string(sourceName) + " unavailable (" + error + ")";
				}
			}
			view.selected = view.packs.empty() ? -1 : 0;
		});
	});
}

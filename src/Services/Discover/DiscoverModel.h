#pragma once

#include "Model/Modpack.h"
#include "Network/Modrinth.h"
#include "Network/PackSources.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

// How long the search input must sit unchanged before a search is sent (seconds).
inline constexpr double kSearchDelaySeconds = 0.4;

// One source's query, filters and results for Discover's combined search.
struct SourceView {
	PackSource source = PackSource::Modrinth;
	SourceCapabilities caps;
	char search[128] = "";
	char gameVersion[32] = "";
	int loader = 0; // 0 = any, else caps.loaders[loader - 1]
	std::string category; // Modrinth category key; empty = any
	int sort = 0;   // index into caps.sorts
	std::vector<Modpack> packs;
	int selected = -1;
	// Typing schedules a search; it runs once the input has been unchanged for
	// kSearchDelaySeconds. The first visit runs the default (empty) search.
	bool searchPending = true;
	double lastSearchEdit = -1.0;
	bool searching = false;
	int searchGeneration = 0; // results from older searches are ignored
	double resultsAt = -1000.0; // the clock's time when the current results arrived
	std::string searchStatus;
	bool searchFailed = false;
	std::uint64_t totalHits = 0;
	int revision = 0; // bumped whenever packs is replaced or extended, so Discover re-merges
	bool loadingMore = false; // fetching the next page for "Load more"
	std::string loadMoreError;
	// True when the source has results beyond the ones loaded.
	bool HasMore() const { return !searchFailed && !searching && packs.size() < totalHits; }
};

// Discover's combined search: one query and set of filters, sent to every selected
// source at once. Each source's results stay in its SourceView; the list shown is
// merged from them.
struct DiscoverState {
	char search[128] = "";
	char gameVersion[32] = "";
	std::string loader; // "" = any, else "forge", "neoforge", "fabric" or "quilt"
	// A Modrinth category ("adventure", "tech", ...); "" = any. Sources that can't filter by
	// category are skipped while one is chosen. The list loads the first time the menu opens.
	std::string category;
	std::vector<ModrinthCategory> categories;
	bool categoriesLoading = false;
	std::string categoriesError;
	std::set<PackSource> sources; // the source filter; empty = every source
	// With one source: that source's sort index. With several: 0 best match
	// (the sources' results interleaved by rank), 1 most downloads, 2 name.
	int sort = 0;
	bool searchPending = true;
	double lastSearchEdit = -1.0;
	std::vector<PackSource> activeSources; // what the last search was sent to
	std::vector<std::string> skippedSources; // and why the others weren't, for the status line
	std::vector<std::pair<PackSource, std::size_t>> merged; // (source view, index into its packs)
	std::string mergedKey; // the sort and source revisions merged was built from
	std::string selectedId;
};

// Which sources the current filters send a search to, and why the others are left out.
struct DiscoverPlan {
	std::vector<PackSource> active;
	std::vector<std::string> skipped;
};

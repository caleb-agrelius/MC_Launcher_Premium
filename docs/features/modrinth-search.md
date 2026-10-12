# Modrinth Search

Live modpack search and version lookup against the public Modrinth API (`https://api.modrinth.com/v2`). No API key is needed. Modrinth is the first Discover sub-page; the UI calls it through the source dispatcher in [pack-sources.md](pack-sources.md). The UI side is described in [discover.md](discover.md).

## Client (`src/Network/Modrinth.{h,cpp}`)
- `SearchModrinthModpacks(http, query, options, limit, out, err, cancel)`
  - Calls `GET /search` with `limit` and `facets`. The facets are AND-ed groups: always `["project_type:modpack"]`, plus `["versions:<mc version>"]` and `["categories:<loader>"]` when `ModrinthSearchOptions` sets them.
  - Sorts by `options.index` (`downloads`, `follows`, `newest`, `updated`). When that is empty, it sorts by `downloads` for an empty query and by `relevance` otherwise.
  - Each hit becomes a `Modpack`:

    | Field | Source |
    |---|---|
    | `id` | `slug` (falls back to `project_id`) |
    | `name` | `title` |
    | `description` | `description` |
    | `author` | `author` |
    | `downloads` | `downloads` |
    | `gameVersion` | last entry of `versions` |
    | `iconUrl` | `icon_url` (png or webp; may be null) |
    | `bannerUrl` | `featured_gallery`, else the first `gallery` entry (350px thumbnails) |
    | `color` | `color` (0xRRGGBB) |
    | `source`, `sourceId` | `PackSource::Modrinth`, the slug |
    | `websiteUrl` | `https://modrinth.com/modpack/<slug>` |
    | flags | `filesLoaded = false` |

  - `out.totalHits` is the total match count.
- `FetchLatestModrinthVersion(http, slugOrId, out, err, cancel)`
  - Calls `GET /project/{slug}/version`, which lists versions newest first.
  - Picks the newest `version_type == "release"`, or the newest version if there's no release.
  - Fills `versionNumber`, `gameVersion` (last of `game_versions`), `loaders` (comma-joined), and `files`.
  - `files` holds a single `PackFile`: the version's primary file, or else its first `.mrpack`.
  - Also fills `id` (the version id, used as `sourceVersionId`), `versionType` and `published` (the date part of `date_published`). Parsing is shared with the list below (`ParseModrinthVersion`).
- `ListModrinthVersions(http, slugOrId, out, err, cancel)` returns every version from the same endpoint that has an `.mrpack`, newest first, for the version picker.
- `FetchModrinthCategories(http, out, err, cancel)`
  - Calls `GET /tag/category` and keeps the tags with `project_type == "modpack"` and `header == "categories"` (not the feature, resolution or performance tags).
  - Each becomes a `ModrinthCategory`: the `name` as the key, and a label with dashes turned into spaces and a capital first letter (`kitchen-sink` -> "Kitchen sink"), sorted by label.
  - `ModrinthSearchOptions::category` adds a `["categories:<key>"]` facet, AND-ed with the others. `live_checks` (`CheckCategories`) confirms the list includes Adventure and that the Adventure filter narrows the most-downloaded list.
- `FetchModrinthBanner(http, slugOrId, outUrl, err, cancel)`
  - Calls `GET /project/{slug}` and picks the gallery image marked `featured`, else the first one.
  - Sets `outUrl` to its `raw_url` (the original upload), else its `url`. `outUrl` is empty if the project has no gallery.
  - The UI stores the result in `Modpack::bannerHiResUrl` and sets `bannerLoaded`.
- All three return `false` with `err` set on HTTP errors, unexpected JSON, a project with no versions, or a version without a `.mrpack`.

## HTTP (`src/Network/HttpClient.{h,cpp}`)
- `HttpClient::Get(url, body, err, cancel, headers)` fetches into memory, with a 20 s connect timeout and a 60 s total timeout.
- `HttpClient::Post(url, jsonBody, body, err, cancel, headers)` sends JSON (`Content-Type` and `Accept: application/json`) with the same timeouts.
- `headers` are extra request headers, e.g. an API key. HTTP errors are reported as `"HTTP <status>"`.
- Each thread reuses one curl handle, so repeated requests keep their connection open. `HttpClient::ReleaseThreadHandle()` frees the calling thread's handle early; workers call it when their task ends. The thread that destroys `HttpClient` frees its own handle first; every other thread that made requests must have exited by then (the app joins its workers and image threads before that).
- `HttpClient::UrlEncode()` percent-encodes query parameters.
- Every request sends `User-Agent: MC_Launcher_Premium/0.1` (`HttpClient::kUserAgent`), as Modrinth asks clients to identify themselves. Modrinth's rate limit is 300 requests per minute.

## Threading (`src/Core/`, `src/Services/Discover/`)
This applies to every source.
- Searches, version lookups, and banner lookups run on worker threads (the service's `TaskScope` over `WorkerPool`), with the shutdown flag as the cancel flag.
- Results are posted to `UiQueue`, which is drained at the start of each frame. A sub-page's pack list, selection, and the lookup state (`filesLoading`, `filesErrors`) are therefore only modified on the UI thread.
- Each sub-page's searches get a `searchGeneration` number. Results from an older search than that sub-page's latest are dropped, so typing quickly can't show stale results.
- Version-lookup and banner-lookup results are applied to every pack in the sub-page's current list with a matching `id`. If the list has changed since the lookup started, the result is discarded.

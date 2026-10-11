# Modrinth Search

Live modpack search and version lookup against the public Modrinth API (`https://api.modrinth.com/v2`). No API key is needed. The UI side is described in [discover.md](discover.md).

## Client (`src/Network/Modrinth.{h,cpp}`)
- `SearchModrinthModpacks(http, query, limit, out, err, cancel)`
  - Calls `GET /search` with `facets=[["project_type:modpack"]]` and `limit`. It sorts by `downloads` for an empty query and by `relevance` otherwise.
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
    | flags | `fromModrinth = true`, `filesLoaded = false` |

  - `out.totalHits` is the total match count.
- `FetchLatestModrinthVersion(http, slugOrId, out, err, cancel)`
  - Calls `GET /project/{slug}/version`, which lists versions newest first.
  - Picks the newest `version_type == "release"`, or the newest version if there's no release.
  - Fills `versionNumber`, `gameVersion` (last of `game_versions`), `loaders` (comma-joined), and `files`.
  - `files` holds a single `PackFile`: the version's primary file, or else its first `.mrpack`.
- `FetchModrinthBanner(http, slugOrId, outUrl, err, cancel)`
  - Calls `GET /project/{slug}` and picks the gallery image marked `featured`, else the first one.
  - Sets `outUrl` to its `raw_url` (the original upload), else its `url`. `outUrl` is empty if the project has no gallery.
  - The UI stores the result in `Modpack::bannerHiResUrl` and sets `bannerLoaded`.
- All three return `false` with `err` set on HTTP errors, unexpected JSON, a project with no versions, or a version without a `.mrpack`.

## HTTP (`src/Network/HttpClient.{h,cpp}`)
- `HttpClient::Get(url, body, err, cancel)` fetches into memory, with a 20 s connect timeout and a 60 s total timeout.
- `HttpClient::UrlEncode()` percent-encodes query parameters.
- Every request sends `User-Agent: MC_Launcher_Premium/0.1` (`HttpClient::kUserAgent`), as Modrinth asks clients to identify themselves. Modrinth's rate limit is 300 requests per minute.

## Threading (`src/main.cpp`)
- Searches, version lookups, and banner lookups run on worker threads (`StartWorker`), with `shuttingDown` as the cancel flag.
- Results are posted to `UiQueue`, which is drained at the start of each frame. The pack list, selection, and lookup state (`filesLoading`, `filesErrors`) are therefore only modified on the UI thread.
- Each search gets a `searchGeneration` number. Results from an older search than the latest are dropped, so typing quickly can't show stale results.
- Version-lookup and banner-lookup results are applied to every pack in the current list with a matching `id`. If the list has changed since the lookup started, the result is discarded.

## Not yet supported
- Paging beyond the first 30 results.
- Filters (Minecraft version, loader, category).
- Choosing a version other than the latest.
- CurseForge.

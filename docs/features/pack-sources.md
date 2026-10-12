# Pack Sources, Search Filters and Install Plans

How Discover talks to more than one modpack site, and how whole packs are installed from a plan. Each source has its own doc for its API details: [modrinth-search.md](modrinth-search.md), [curseforge.md](curseforge.md), [ftb.md](ftb.md), [technic.md](technic.md), [atlauncher.md](atlauncher.md).

## Dispatcher (`src/Network/PackSources.{h,cpp}`)
The UI calls these functions with a `PackSource`, and they call that source's client. Inside `PackSources.cpp` each source is a `PackSourceClient` subclass (`ModrinthSource`, `CurseForgeSource`, `FtbSource`, `TechnicSource`, `AtLauncherSource`) that overrides what it supports; the base class answers everything else with a "<Source> ... isn't supported" error. `Client(source)` picks the instance, so adding a source means one new subclass and one line there.

| Function | What it does |
|---|---|
| `OnlineSources()` | The sources in filter order: Modrinth, CurseForge, FTB, Technic, ATLauncher. `PackSource::Custom` (key `custom`) is for packs the player made and isn't searched (see [library.md](library.md#new-packs)) |
| `GetSourceCapabilities(source)` | Which filters and sorts the source supports, its search hint, its list heading, and `installsPacks` |
| `IsSourceAvailable(source, reason)` | False with a reason when a source can't be used (CurseForge without an API key) |
| `SearchPacks(http, source, query, filters, limit, out, err, cancel)` | Searches; results have `filesLoaded = false` |
| `FetchPackVersion(http, pack, out, err, cancel)` | The version a pack installs by default, plus details the search didn't include |
| `ListPackVersions(http, pack, out, err, cancel)` | Every version, newest first, for the version picker. Each `PackVersionInfo` also has `versionType` and `published` when the source gives them, and can be passed to `ApplyPackVersion` |
| `ApplyPackVersion(pack, version)` | Copies a version result onto the pack; empty fields keep what the pack had |
| `FetchPackDescription(http, pack, out, err, cancel)` | The pack's full description as plain text (see "Descriptions" below) |
| `FetchPackMods(http, pack, workDir, out, err, cancel)` | The mods in the pack's loaded version (`sourceVersionId`), sorted by name, for Discover's Mods tab (see the table below) |
| `BuildInstallPlan(http, pack, workDir, plan, status, err, cancel)` | Turns the pack's version into an `InstallPlan` (every source except Modrinth) |

`Modpack` carries `source`, the source's own ids (`sourceId`, `sourceVersionId`), `websiteUrl`, and two Technic-only fields (see [modpack-index.md](modpack-index.md)). Pack ids are unique across sources.

### Descriptions

`FetchPackDescription` fetches the long description and converts it with `MarkupToPlainText` (`src/Util/Text.{h,cpp}`). The converter drops HTML tags (`script`/`style` content included), images and link targets, keeps link text, removes heading, emphasis, code, quote and rule markers, turns list items into "·" lines and table rows into space-separated cells, decodes common entities, collapses blank lines, and swaps curly quotes, dashes and ellipses for ASCII, because the UI font lacks them.

| Source | Description |
|---|---|
| Modrinth | The project's Markdown `body` (`GET /project/{id}`) |
| CurseForge | The HTML from `GET /mods/{id}/description` |
| FTB | The pack's Markdown `description` (`/modpack/{id}`); search results only carry the `synopsis` |
| Technic, ATLauncher | The description already in the search results, so no request is made |

If the source returns nothing, the pack's summary is used.

### Changelogs
- `FetchPackChangelog(http, pack, out)` returns the changelog of the pack's version (`pack.sourceVersionId`) as plain text (`MarkupToPlainText`). Sources that have them set `SourceCapabilities::changelogs`:
  - Modrinth: `GET /v2/version/{id}` `changelog` (Markdown). A pack installed before versions had ids is matched by its version number in `/project/{id}/version`.
  - CurseForge: `GET /v1/mods/{id}/files/{fileId}/changelog` (HTML).
  - FTB: `GET /modpack/{id}/{versionId}/changelog` `content` (Markdown).
- Technic and ATLauncher have none; the call fails with "<source> doesn't publish changelogs". Used by the Manage page's Changelog tab ([manage.md](manage.md)). `live_checks` fetches one per source that has them.

### Mod lists

`FetchPackMods` fills a `PackModList`: one `PackMod` per mod (name, summary, version or file name, page URL, and client-only, server-only and optional flags when the source gives them), plus a note.

| Source | Where the list comes from |
|---|---|
| Modrinth | The version's `embedded` dependencies (`GET /version/{id}`), then `GET /projects?ids=[...]` in batches of 100 for titles, summaries and sides (`server_side: unsupported` means client only). `FetchPackVersion` stores the version id in `sourceVersionId`. Files hosted outside Modrinth aren't dependencies, so the note says only Modrinth-hosted mods are listed. |
| CurseForge | The pack zip's `manifest.json`, downloaded into `downloads/<pack id>/` exactly like an install (the same SHA-1-checked cache, so installing afterwards reuses it), then `POST /mods` for names, summaries and links. Resource packs and shader packs are labelled. Files the manifest marks not required are optional. |
| FTB | The version manifest's files with `type: mod`, named after their file. `clientonly`, `serveronly` and `optional` come from the manifest. CurseForge-hosted files link to their project. |
| Technic | Solder packs: the build's mods (`?include=mods` for pretty names, descriptions and links). Single-zip packs return an empty list and a note. |
| ATLauncher | `Configs.json` mods, without the loader (`forge`, `mcpc`) and `library` or `hidden` entries. `client`/`server` give the sides. |

## Search filters
`SearchFilters` holds a Minecraft version (free text, e.g. `1.20.1`), a loader key (`forge`, `neoforge`, `fabric`, `quilt`), a sort index, a Modrinth `category` key (only sources whose capabilities set `categoryFilter` honor it; Discover skips the rest while one is chosen), and an `offset`: how many results to skip, for Discover's **Load more**. Modrinth passes it as `offset`, CurseForge as `index`. FTB, Technic and ATLauncher search a full list locally and return the slice after it; Technic also fetches details for `(offset + limit) * 2` packs when filtering or sorting, so later pages are filtered too. `live_checks` (`CheckPaging`) confirms page 2 continues page 1 without repeats for Modrinth, FTB, ATLauncher and Technic. `SourceCapabilities` says which apply:

| Source | Minecraft version | Loader | Sorts |
|---|---|---|---|
| Modrinth | yes | Forge, NeoForge, Fabric, Quilt | Best match, Downloads, Follows, Newest, Recently updated |
| CurseForge | yes | Forge, NeoForge, Fabric, Quilt | Best match, Popularity, Total downloads, Last updated, Name |
| FTB | yes (prefix: `1.20` matches `1.20.x`) | Forge, NeoForge, Fabric, Quilt | Featured, Most played, Recently updated, Name |
| Technic | yes (prefix; checked against each result's details) | no (Technic doesn't expose loaders) | Trending, Most installed |
| ATLauncher | yes (prefix) | no (the pack list has no loader info) | Featured, Name |

Sort index 0 is the source's default. For Modrinth, "Best match" means most downloaded for an empty query and relevance otherwise.

## Install plans (`src/Storage/InstallPlan.{h,cpp}`)
Sources whose capabilities set `installsPacks` (every source except Modrinth) install a whole pack in one step instead of the per-file download and extract flow. Discover's **Install pack** card drives it (see [discover.md](discover.md)).

- **`InstallPlan`** holds:
  - the source and its ids, the version name, the Minecraft version, and the loader and its version
  - `files`, a list of `PlannedFile`s: a URL, a path relative to the game folder, an optional SHA-1 or MD5 and size, and an `extract` flag with an optional `extractPrefix` for zips to unpack instead of save
  - `overrides`: local zips merged into the game folder, each with a folder prefix to take entries from
  - `notes` on anything skipped, and `maxConcurrent`
- **`RunInstallPlan(http, plan, installDir, progress, blocked, err, cancel)`** works through these steps:
  1. Rejects the whole plan if any file path is unsafe (`IsSafeRelativePath`).
  2. Moves files with no URL to `blocked`. These are files the user has to download by hand. Each carries `manualPageUrl` and `blockedReason`. They don't fail the install.
  3. Downloads the rest on `maxConcurrent` threads into `installDir/minecraft/<relPath>`. Paths past Windows' 260-character limit work (see "Long paths" below).
     - Each file is first written as `<name>.part`, then checked against its SHA-1, MD5 or size, then renamed.
     - A file already on disk that passes the check is kept, so a retry only fetches what's missing.
     - Files in `plan.skipFiles` (pack files the player removed) and files with a `<name>.disabled` copy (turned off by the player) are skipped. After a successful install, `DownloadService` re-applies the version's content changes (`ApplyContentChanges`; see [manage.md](manage.md#your-changes)).
     - Extract files are downloaded to `installDir/.cache/`, checked, unpacked into `relPath`, and deleted.
  4. Deletes files the previous install of this pack listed in its `pack.json` `files` and this plan doesn't (e.g. mods dropped when switching versions). Only files installed by name are tracked, not the contents of extracted zips.
  5. Extracts the override zips. They go last, so a pack's own config files win.
  6. Removes `.cache/` and writes `installDir/pack.json` with the source, ids, version, Minecraft version, loader, blocked files, notes, `files` (the plan's non-extract paths), and `clientOnlyFiles` (paths the source marks client-only: FTB `clientonly`, ATLauncher `server: false`; dedicated servers copy these turned off, see [servers.md](servers.md)).
- **Errors:** the first failing file stops the install with `"<file>: <reason>"`. Reasons include `download failed: HTTP 404`, `SHA-1 mismatch` and `extract failed: ...`. Progress is reported as a stage name with done/total counts.

## Long paths (`src/Util/Paths.{h,cpp}`)
Packs nest config files deeply, so installs can pass Windows' 260-character path limit.
- `LongPath(path)` returns long paths in their absolute `\\?\` form.
- `ExtendedPath(path)` always returns the absolute `\\?\` form. Use it for a folder that is walked or copied recursively, because paths built under a short root can still pass the limit.
- `OpenFile(path, mode)` is a `_wfopen` that accepts such paths.

These are used by `HttpClient::DownloadToFile`, the hash functions, `ExtractZip`, and `RunInstallPlan`. Dedicated server setup and deletion use `ExtendedPath` (see [servers.md](servers.md)).

## Verification
Each source was checked against its live API with a scratch harness built from the non-UI sources (search, every filter and sort, version lookup, plan, and a full install). The pack used for each source is named in that source's doc.

`tests/LiveChecks.cpp` (the `live_checks` target, not run by CTest) repeats the version lookup and mod list for each source against the live APIs. It skips CurseForge unless `MCLP_CURSEFORGE_API_KEY` is set. See [development.md](../development.md#regression-checks).

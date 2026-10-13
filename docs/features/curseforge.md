# CurseForge

The CurseForge Discover sub-page (search, version lookup, and whole-pack installs) and a second source for Manage > Content > Browse, through the CurseForge API (`https://api.curseforge.com/v1`). Code: `src/Network/CurseForge.{h,cpp}`, called through [pack-sources.md](pack-sources.md) and `src/Network/ModSearch.cpp`.

## API key
- Every request sends `x-api-key`. `CurseForgeApiKey()` returns the key from Settings > Downloads (`curseForgeApiKey` in `settings.json`) if it's set, otherwise the `MCLP_CURSEFORGE_API_KEY` environment variable. It is safe to call from any thread.
- `main()` passes the Settings value to `SetCurseForgeApiKey()` at startup and whenever the Settings page changes. Surrounding spaces are trimmed. When the effective key changes, the CurseForge sub-page searches again.
- The key is never built into the app or stored in the repo. `settings.json` is gitignored. Setup steps are in the README.
- With no key, the sub-page is disabled and shows "CurseForge needs an API key. Add one in Settings > Downloads, or set the MCLP_CURSEFORGE_API_KEY environment variable". No request is sent.
- A rejected key (HTTP 403) shows as "CurseForge unavailable (CurseForge rejected the API key (HTTP 403))".

## Search (`SearchCurseForgeModpacks`)
`GET /mods/search` with:
- `gameId=432` (Minecraft) and `classId=4471` (modpacks)
- `pageSize` (30)
- `searchFilter` (the query), `gameVersion`, and `modLoaderType` (Forge 1, Fabric 4, Quilt 5, NeoForge 6)
- `sortField` and `sortOrder`:

  | Sort label | sortField |
  |---|---|
  | Best match | none with a query (CurseForge's ranking); 6 (total downloads) without one |
  | Popularity | 2 |
  | Total downloads | 6 |
  | Last updated | 3 |
  | Name | 4 (ascending) |

| `Modpack` field | CurseForge field |
|---|---|
| `id` | `"cf-" + id` |
| `sourceId` | `id` |
| `name`, `description` | `name`, `summary` |
| `author` | `authors[0].name` |
| `downloads` | `downloadCount` |
| `iconUrl` | `logo.thumbnailUrl` |
| `bannerUrl` | `screenshots[0].url` |
| `websiteUrl` | `links.websiteUrl` |
| `gameVersion` | first `latestFilesIndexes[].gameVersion` |
| `loaders` | the distinct `latestFilesIndexes[].modLoader` values |

`pagination.totalCount` is the match count.

## Version (`FetchCurseForgeVersion`)
- Calls `GET /mods/{id}` and picks the newest `latestFiles` entry (highest id) with `releaseType == 1` (release), else the newest of any type.
- Sets the version name (`displayName`) and `sourceVersionId` (the file id).
- Sets the Minecraft version and loaders, taken from the file's `gameVersions`, which mixes versions like `1.20.1` with loader names like `Forge`.

## Version list (`ListCurseForgeVersions`)
- Calls `GET /mods/{id}/files?index=<n>&pageSize=50` page by page until `pagination.totalCount` is reached, up to 500 files.
- Each file becomes a version: `displayName` (else `fileName`), its id, `releaseType` 1/2/3 as release/beta/alpha, the date part of `fileDate`, and the Minecraft version and loaders from `gameVersions`. Sorted by id, newest first.
- Not checked against the live API yet (no API key was available).

## Install (`BuildCurseForgePlan`)
1. **Pack zip.** Calls `GET /mods/{id}/files/{fileId}` and downloads the zip into `downloads/<pack id>/`. A zip already there with a matching SHA-1 is reused. If the pack itself has no `downloadUrl`, the install fails with "This pack's author only allows downloads from the CurseForge website".
2. **Manifest.** Reads `manifest.json` from the zip (`ParseCurseForgeManifest`).
   - Requires `manifestType == "minecraftModpack"` and `manifestVersion == 1`.
   - The loader comes from the `primary` mod loader id. `forge-47.2.0` becomes `forge` and `47.2.0`.
   - Files with `required: false` are skipped and noted.
3. **Files** (`ResolveCurseForgeFiles`, also used by FTB):
   - One `POST /mods/files` call with every file id, and one `POST /mods` call with every project id.
   - Each file goes into the folder for its project's class: `mods/`, `resourcepacks/` (12), `shaderpacks/` (6552), or `datapacks/` (6945).
   - Each file is checked against its SHA-1 (`hashes[].algo == 1`).
4. **Blocked files.** Some authors turn off third-party downloads, so their files have no `downloadUrl`.
   - `FillFromModrinthByHash` looks their SHA-1s up with Modrinth's `POST /v2/version_files` and uses the Modrinth file when one matches.
   - Files still missing are listed on Discover with a link to `<project page>/download/<fileId>` to download them by hand.
   - The launcher never builds CDN URLs for them itself.
5. **Overrides.** The zip's `<overrides>/` folder is merged into the game folder last. Downloads run 6 at a time.

## Browse (mods, resource packs and shaders)
Manage > Content > Browse can search CurseForge instead of Modrinth (see [manage.md](manage.md#browse)). `ModSearch.cpp` sends a search whose `ContentSearch::source` is `CurseForge` to these functions in `CurseForge.cpp`. Without a key they fail with the same "CurseForge needs an API key" message as Discover, before any request.
- **Project ids** are `cf-` and the CurseForge id (`cf-394468`), so they never collide with Modrinth's in the browser's installed set.
- **Search** (`SearchCurseForgeContent`): `GET /mods/search` with `gameId=432`, `classId` 6 (mods), 12 (resource packs) or 6552 (shaders), `pageSize`, the pack's `gameVersion`, and for mods `modLoaderType` (Forge 1, Fabric 4, Quilt 5, NeoForge 6). With no text it sorts by total downloads (`sortField=6`); with text it leaves CurseForge's own ranking. A Quilt pack searches Quilt and then Fabric, merges the lists without duplicates and puts the most downloaded first.

  | `ContentProject` field | CurseForge field |
  |---|---|
  | `id` | `"cf-" + id` |
  | `name`, `summary` | `name`, `summary` |
  | `author` | `authors[0].name` |
  | `iconUrl` | `logo.thumbnailUrl` |
  | `pageUrl` | `links.websiteUrl` |
  | `downloads` | `downloadCount` |
- **File** (`FindCurseForgeContentDownload`): `GET /mods/{id}/files?pageSize=50` with `gameVersion` and `modLoaderType` (a Quilt pack tries Quilt, then Fabric). It takes the newest release (`releaseType == 1`, highest file id), else the newest file of any kind. The file's name, `downloadUrl`, size (`fileLength`) and SHA-1 (`hashes[].algo == 1`) fill a `ContentDownload`, and its `dependencies` with `relationType` 3 (required) become required projects. No fitting file gives "No version for Minecraft 1.20.1 with fabric."
- **Blocked files:** when an author turns off third-party downloads the file has no `downloadUrl`. The SHA-1 is looked up on Modrinth (`FillFromModrinthByHash`) and that copy is used if it exists. Otherwise the error says "The author of <file> only allows downloads from the CurseForge website" with a link to `<project page>/download/<fileId>` (the page comes from `GET /mods/{id}`). The launcher never builds CDN URLs itself.
- **Installed projects** (`FindCurseForgeInstalledProjects`): CurseForge identifies a file by its fingerprint, the MurmurHash2 (seed 1) of its bytes with tabs, line feeds, carriage returns and spaces left out (`CurseForgeFingerprint` in `Util/Hash`; the empty file's is 1540447798). Every regular file in the folder, turned off or not, is hashed in two passes (the first counts the bytes that are hashed, because the length is part of the hash's start) and sent in batches of 500 to `POST /fingerprints/432`. Each entry of `data.exactMatches` gives a project (`id`, else `file.modId`).
- **Install** is `InstallContent` in `ModSearch.cpp`, shared with Modrinth: the mod, then the required mods it names, each checked against its SHA-1 before it is renamed into place and shared between packs (see [shared-files.md](shared-files.md)).

## Verification
The Browse functions are checked in `launcher_checks` against scripted replies in CurseForge's documented shapes (search with class, version and loader in the request, the newest release chosen over a newer beta, dependencies, a blocked file with and without a Modrinth match, fingerprints sent and matched, a full install with its dependency, a failed SHA-1 refused, no request without a key), and the fingerprint against values from a separate implementation of MurmurHash2. None of it has met the live API: the fingerprint endpoint's request and response shape, `modLoaderType` and `gameVersion` filters on `/files`, and the `relationType` numbers are from the documentation. `live_checks` has Browse checks (search, install with dependencies, installed by fingerprint) that run when `MCLP_CURSEFORGE_API_KEY` is set.

Discover, checked without a key: manifest parsing (valid, wrong type, and broken JSON), the Modrinth fallback (one real Sodium jar found by SHA-1, and one unknown hash left blocked), and the request path (CurseForge answers a placeholder key with HTTP 403). Live search, version lookup, and installs haven't been run, because no API key was available. They follow CurseForge's documented response shapes.

# CurseForge

The CurseForge Discover sub-page: search, version lookup, and whole-pack installs through the CurseForge API (`https://api.curseforge.com/v1`). Code: `src/Network/CurseForge.{h,cpp}`, called through [pack-sources.md](pack-sources.md).

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

## Verification
Checked without a key: manifest parsing (valid, wrong type, and broken JSON), the Modrinth fallback (one real Sodium jar found by SHA-1, and one unknown hash left blocked), and the request path (CurseForge answers a placeholder key with HTTP 403). Live search, version lookup, and installs haven't been run, because no API key was available. They follow CurseForge's documented response shapes.

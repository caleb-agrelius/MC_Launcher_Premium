# FTB

The FTB (Feed The Beast) Discover sub-page: search, version lookup, and whole-pack installs through the public FTB API (`https://api.feed-the-beast.com/v1/modpacks/public`). No key is needed. Code: `src/Network/Ftb.{h,cpp}`, called through [pack-sources.md](pack-sources.md).

## Search (`SearchFtbModpacks`)
The API has no search endpoint, so the first search loads everything:
- **First search:** `GET /modpack/all` lists about a hundred pack ids. Each pack's details (`GET /modpack/{id}`) are then fetched on 4 threads (about 3 s), and private packs are left out.
- **Later searches:** the list is cached for the session, so they filter it in memory. A partly failed load isn't cached, so the next search retries.
- **Matching:** every word of the query must appear in the pack's name, synopsis or tags. For text searches with the default sort, packs whose name contains the words come first.
- **Filters:**
  - Minecraft version: `1.20` matches `1.20` and `1.20.x`
  - Loader: Forge, NeoForge, Fabric or Quilt

  Both are read from the default version's `targets`.
- **Sorts:**
  - **Featured** (default): featured packs first, then most installed
  - **Most played**
  - **Recently updated**
  - **Name**

| `Modpack` field | FTB field |
|---|---|
| `id` | `"ftb-" + id` |
| `sourceId` | `id` |
| `name`, `description` | `name`, `synopsis` |
| `author` | `authors[0].name` |
| `downloads` | `installs` |
| `iconUrl` / `bannerUrl` | the `art` entries of type `square` / `splash` |
| `websiteUrl` | `https://www.feed-the-beast.com/modpacks/<id>-<slug>` |
| `gameVersion`, `loaders`, `version` | the default version's `targets` (`game` / `modloader`) and name |

## Version (`FetchFtbVersion`)
The default version is the newest one (highest id) of type `release`, else the newest that isn't `archived`, else the newest. Its id becomes `sourceVersionId`.

## Install (`BuildFtbPlan`)
- **Manifest:** `GET /modpack/{id}/{versionId}` lists `targets` (Minecraft version and loader) and `files`.
- **Files:**
  - Each file goes to `<path>/<name>` with the leading `./` removed, and is checked against its `sha1`.
  - `serveronly` files are skipped and noted.
  - A path that would escape the instance fails the plan.
- **CurseForge-hosted files:** a file with no `url` but a `curseforge {project, file}` entry is resolved through the CurseForge API (see [curseforge.md](curseforge.md)) when a key is configured. Otherwise it is listed as blocked, with a link to `https://www.curseforge.com/projects/<project>`. No current FTB pack has such files, but the API allows them.
- **One at a time:** files download one by one (`maxConcurrent = 1`). Prism Launcher's code notes that "FTB blocks multiple requests at a time". The per-thread curl connection reuse keeps this reasonably fast.
- There is no override zip; every file is listed in the manifest.

## Verification
Checked against the live API with a scratch harness:
- **Search, filters and sorts:** browse (95 packs), search, the `1.20` + Fabric filter, and all four sorts.
- **Version lookup:** passed.
- **Full install:** FTB Magic World 2 1.1.2 (Minecraft 1.6.4, Forge 9.11.1.965) installed 911 files in about 75 s.
  - Some of its file URLs contain spaces; `HttpClient` escapes those.
  - Some of its config paths pass Windows' 260-character limit; `Util/Paths` handles those.
- **Sub-page in the app:** the sub-page was also checked in the running app.

# ATLauncher

The ATLauncher Discover sub-page: search, version lookup, and whole-pack installs from ATLauncher's static CDN (`https://download.nodecdn.net/containers/atl/`). Code: `src/Network/AtLauncher.{h,cpp}`, called through [pack-sources.md](pack-sources.md). No key is needed.

## Pack list and search (`SearchAtLauncherModpacks`)
- `launcher/json/packsnew.json` lists every pack. It's fetched once per run, cached, and searched locally.
- Only `type: "public"` packs with at least one version are kept (about 140).
- **Safe name:** a pack's folder on the CDN is its name with everything but letters and digits removed (`Sky Factory 4` → `SkyFactory4`).
- **Mapping:**
  - `id` = `"atl-" + safeName`
  - `sourceId` = the safe name
  - `iconUrl` = `launcher/images/<lowercase safe name>.png`
  - `websiteUrl` = the pack's `websiteURL`, else `https://atlauncher.com/pack/<safeName>`
  - `version` and `gameVersion` come from the first (newest) entry in `versions`. The author is shown as "ATLauncher". There are no download counts.
- **Text search:** substring match on the name and description, with name matches listed first.
- **Filters:** **Minecraft version**, a prefix match (`1.12` matches `1.12.x`). There's no loader filter, because the pack list doesn't say which loader a pack uses.
- **Sorts:** **Featured** (featured packs first, then ATLauncher's `position`) and **Name**.

## Versions (`ListAtLauncherVersions`, `FetchAtLauncherVersion`)
- The pack list keeps every version with its Minecraft version (newest first), which `ListAtLauncherVersions` returns for the version picker. The loader is only known from a version's `Configs.json`.
- The default is the newest version.
- Reads `packs/<safeName>/versions/<version>/Configs.json` to fill in the Minecraft version and the loader type (`loader.type`).

## Install (`BuildAtLauncherPlan`)
- **Configs:** unless `noConfigs` is set, `Configs.zip` (same folder as `Configs.json`) is downloaded into the work folder, checked against `configs.sha1`, and merged into `minecraft/` last as an override.
- **Loader:** `loader.type` and `loader.metadata.version` (else `metadata.loader`) are recorded in the plan.
- **Mods:** each `mods[]` entry becomes a planned file, checked by `md5` and `filesize`. 4 download at a time.
  - Skipped: `client: false` mods, and `optional` mods that aren't named in `pack.optionalMods` (with a note counting them).
  - **Optional mods** (`FetchAtLauncherOptionalMods`): every client mod with `optional: true` becomes an `OptionalMod` with its `name`, `description`, `group`, `depends` and default (`selected` or `recommended`). Discover's picker uses them (see [discover.md](discover.md)), and the choice is saved in `library.json` so **Repair files** and **Reinstall** keep it.
  - `download: "server"` → the CDN base + `url`. `"direct"` → `url` as-is. `"browser"` → a blocked file whose page is `url`.
  - Folder by `type`:

    | Type | Folder |
    |---|---|
    | `root` | game folder |
    | `mods` | `mods` |
    | `flan` | `Flan` |
    | `dependency` (and the misspelled `depandency`) | `mods/<minecraft version>` |
    | `ic2lib`, `denlib` | `mods/ic2`, `mods/denlib` |
    | `coremods`, `plugins` | same name |
    | `texturepack`, `resourcepack`, `shaderpack` | `texturepacks`, `resourcepacks`, `shaderpacks` |
    | `jar`, `forge` | `jarmods`: Forge's universal jar there is detected and installed as the loader, and other jar mods are patched into the game jar at Play (see [launching.md](launching.md)) |

  - `extract` mods are zips unpacked into the folder for `extractTo`. Only the zip folder named by `extractFolder` is taken (ATLauncher's `%s%` means `/`).
  - `texturepackextract` and `resourcepackextract` unpack into `texturepacks/extracted` and `resourcepacks/extracted`.
  - `mcpc`, `decomp`, `millenaire` and unknown types are skipped, with a note.

## Verification
Checked against the live CDN with a scratch harness:
- **Search, filter and sort:** the pack list, a `1.12.2` filter, and the Name sort.
- **Install:** Lithium 1.21.11-1 (10 Fabric mods plus `Configs.zip`).
- **Plan only:** The Last Days, with one `browser` (blocked) file and 59 optional mods skipped.
- **Sub-page in the app:** the sub-page and its details pane were checked in the running app.

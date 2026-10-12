# Discover Page

Browse modpacks and start downloads and installs. UI code is `src/UI/Pages/Discover/`: `DiscoverPage` lays out `DiscoverFilterBar`, `DiscoverResultList` and `DiscoverDetails` (which holds `PackInstallSection`, `PackPlayButton` and `PackInfoTabs`). The search, paging and merging are `DiscoverService`, and the version, banner, description and mod lookups are `PackDetailsService` (`src/Services/Discover/`).

## Combined search
- Discover searches **Modrinth**, **CurseForge**, **FTB**, **Technic** and **ATLauncher** together (see [pack-sources.md](pack-sources.md)). Source chips select one or several sources, or **All sources**.
- `DiscoverState` holds the shared query, filters, sort, merged result references and selected pack id. Each `SourceView` holds one source's results, status and revision.
- The source filter is saved as `discoverSources` in `settings.json`; an empty list means all sources.

## Search
- A full-width text box holds up to 127 characters. Typing searches each active source after a 0.4 s pause (`kSearchDelaySeconds`). An empty query fetches default lists.
- **Filters** sit under the box (see [pack-sources.md](pack-sources.md)):
  - a **Minecraft version** text box (searches after typing pauses)
  - a **loader** combo (Any loader, Forge, NeoForge, Fabric, Quilt)
  - a **category** combo (Any category, then Modrinth's modpack categories such as Adventure, Kitchen sink or Technology). The list comes from `FetchModrinthCategories`, run by `DiscoverService::LoadCategories()` on a worker the first time the combo opens ("Loading categories...", with **Retry** on failure). Only Modrinth can filter by category (`SourceCapabilities::categoryFilter`), so while one is chosen the other sources are skipped with "<source> can't filter by category"
  - a **Sort** combo

  Changing a combo searches immediately.
- Sources without required credentials or support for the current filter are skipped with explanations. Other sources continue searching.
- The status line shows searching source names, displayed results, per-source totals and errors, and skipped-source reasons in a tooltip.
- With several sources, **Best match** interleaves results by source rank; **Most downloads** and **Name** sort the combined list. With one source, its native sorts are available.
- **Offline fallback (Modrinth only):** if the search fails, the list shows packs from `resources/index.json` whose name or description contains the query (case-insensitive). See [modpack-index.md](modpack-index.md).
- Each search shows up to `settings.searchLimit` results (30 by default, 10–100, set in Settings > Downloads). CurseForge caps a page at 50.
- **Load more:** when any searched source has more results than it has returned (`SourceView::HasMore`), a **Load more** button sits at the end of the list. It asks each such source for its next page (`DiscoverService::StartSearch(view, true)`, with `SearchFilters::offset` set to the number already loaded) and adds the results below the current ones. Repeats are skipped, and the selection and scroll position stay put.
  - While pages load, the button becomes "Loading more...". A failed page shows "Couldn't load more: <source>: <error>" and the button stays, so it can be retried.
  - An empty page ends that source's results. A new search discards pages still loading.
  - The merged list keeps its order: with several sources, new pages continue the interleaving by rank, or are sorted in when the list is sorted by downloads or name.
- **Ctrl F** opens Discover and focuses the shared search box.

## Pack list (left, 340px scaled by default)
- Drag the gap between the list and the details pane to resize it (`Widgets::PaneSplitter`). The width is saved as `paneWidths.discover` in `settings.json`.
- The heading is **POPULAR** for unfiltered combined browsing, the source's browse label for single-source browsing, and **RESULTS** otherwise.
- Each pack is a row (`Widgets::PackRow`) with its icon (see [images.md](images.md)), its name in bold (shortened with "..." if too long), and a dimmed line:
  - Online packs: newest supported Minecraft version and download count (e.g. `26.3 · 18.4M downloads`), whichever the source provides, else the author, else the source name (Technic search results until the pack is selected)
  - Local packs: version and file count
- The selected row has a rounded highlight with a green bar inside its left edge (see [ui-theme.md](ui-theme.md)). Rows scrolled out of view don't request their icons.
- Clicking a row selects it. The selected pack id survives result re-merges if still present; otherwise the first result is selected. Combined rows include a source tag. "No modpacks found." appears when a search returns nothing.

## Pack details (right)
- **Hero banner** (`Widgets::HeroBanner`), sized to about a third of the pane width (200–340px, scaled):
  - Background, bottom to top: the pack's Modrinth color with a faint Minecraft-style block pattern; the small gallery thumbnail from the search result; the full-size gallery image once it has loaded. Each image fades in, and is scaled to fill and cropped.
  - With **Gallery banners** turned off in Settings > Appearance, only the color and pattern are shown and no gallery images are downloaded.
  - When a Modrinth pack is selected for the first time (other sources don't do this), a background lookup (`FetchModrinthBanner`) finds the full-size image. If it fails, the thumbnail stays and the lookup isn't retried.
  - A gradient into the panel color keeps the text readable. On top are the pack icon, the name in large bold text, "by <author>", and chips for the Minecraft version, the loaders, and the pack version.
- Below the banner, online packs show a downloads chip (when the count is known) and a link to the pack's page (`websiteUrl`, e.g. `modrinth.com/modpack/<slug>`) that opens in the browser (`ImGui::TextLinkOpenURL`). Then comes the wrapped description.
- **Version lookup:** search results don't include files. When an online pack is selected, its latest version is fetched in the background with `FetchPackVersion` (a spinner with "Loading latest version..."). For Modrinth that fills in the version number, loaders, and the `.mrpack` file. On failure, a red "Couldn't load versions: ..." message and a **Retry** button appear.
- **Version picker:** once the default version has loaded, a dropdown shows the pack's version and lists every version when opened (`VersionPicker::Combo` in `src/UI/Common/VersionPicker.cpp`). The list comes from `ListPackVersions` (see [pack-sources.md](pack-sources.md)), loads in the background the first time the dropdown opens ("Loading versions...", with **Retry** on failure), and is cached per pack for the session. Each row shows the version name, then its Minecraft version, its type when it isn't a release (beta, alpha, recommended, latest), and its date when known; the first row is marked "newest". Picking one applies it to the selected pack (`ApplyPackVersion`); its Minecraft version replaces the old one even when the source doesn't say, so a stale one isn't shown.
  - **Modrinth:** the dropdown sits above the pack files, labelled **Version**, with the Minecraft version and loaders next to it. Picking a version swaps in that version's `.mrpack`, so **Download** and **Install files** act on it.
  - **Whole-pack sources:** the dropdown replaces the version name on the install card, and is disabled while an install runs. **Install pack** installs the picked version; the button reads **Install pack** again when My Library has a different version.
- **Install** (sources with whole-pack installs, e.g. CurseForge) shows one card for the chosen version (the newest release until another is picked):
  - The version dropdown, Minecraft version and loaders, then **Install pack**. The button reads **Retry** after a failure and **Reinstall** once installed.
  - Once installed (when My Library has this version, `FindInstalledVersion`), a green "Installed" line follows, with that version's game folder and the **Modified** chip when the player changed it (see [manage.md](manage.md#your-changes)). Each version has its own install entry (`pack:<id>@<version key>`), so several versions can install at once and side by side.
  - While installing: the stage ("Downloading pack archive", "Resolving mod files", "Downloading files 120 / 312", "Extracting pack files") and a thin progress bar.
  - **ATLauncher packs** first load the version's optional mods (`FetchOptionalMods`). If it has any, an **Optional mods** dialog lists them with checkboxes and descriptions, pre-checked with the author's picks (`selected` or `recommended`), or with the choices saved in My Library when reinstalling. Checking a mod also checks the mods it depends on and unchecks others in its group ("pick one: <group>"); unchecking it unchecks mods that need it. **Install** passes the chosen names on the pack (`Modpack::optionalMods`); **Cancel** installs nothing. If the list can't be loaded, the button becomes **Install without optional mods**. Packs without optional mods install right away.
  - Clicking it runs `DownloadService::StartPlanInstall()` (`src/Services/Downloads/`): `BuildInstallPlan`, then `RunInstallPlan` into the version's install folder (see [library.md](library.md#installed-versions)), on a worker thread (see [pack-sources.md](pack-sources.md)). On success the version is added to the pack in My Library and becomes its active version; other installed versions stay.
  - Afterwards, a failed install shows "Install failed: ...". Files that must be downloaded by hand are listed with a **download page** link, and the plan's notes appear as "Note: ...".
- **Pack files** (Modrinth and local packs) shows each file as a card with its name in bold, its formatted size, and an action on the right that depends on its state:

| File state | Shown |
|---|---|
| Never installed | **Install** button |
| Download finished with an error | **Retry** button plus a red "Download failed: ..." message |
| Downloading | "Downloading" (with a percentage once the total size is known) plus a thin progress bar |
| Extracting | "Extracting" plus a thin progress bar |
| Installed | "Installed" (green), plus the **Modified** chip when the player changed that version |
| Downloaded but not extracted (an earlier session, or extraction failed) | **Install** button, which extracts the existing download |
| Extraction failed | **Install** again, plus a red "Extraction failed: ..." message |

- **Install** and **Retry** are one step: a background download (see [downloads.md](downloads.md)), then extraction into the version's install folder (see [library.md](library.md#installed-versions) and [installation.md](installation.md)) as soon as it finishes. The worker marks the file as extracting under the same lock that records the finished download, so no intermediate button flashes, then the UI thread starts `startInstall`. When extraction succeeds, the version is added to the pack in My Library and becomes active (other installed versions stay), with a "Files extracted" toast. Download failures show a toast. If the launcher is closing when a download finishes, the file stays downloaded and can be extracted later from Discover or the Downloads page.
- A large **PLAY** button sits below the install section. It's enabled when the pack is in My Library and isn't already being prepared or played; clicking it starts playing and switches to My Library. If the version shown is installed, it becomes the active version first; if it isn't, a note says PLAY starts the installed active version instead. Otherwise it's disabled, with a hint to install the pack's file. See [launching.md](launching.md).
- Online packs then show **Description** and **Mods** tabs (`Widgets::TabStrip`). The chosen tab stays chosen when another pack is selected. Switching tabs keeps the details pane at least as tall as the visible area was (`detailsKeepBottom`, reset when another pack is selected), so a shorter or still-loading tab doesn't throw the scroll position back to the top.
- The **Description** tab shows the pack's full description (`FetchPackDescription`, see [pack-sources.md](pack-sources.md#descriptions)) as wrapped plain text, while the line under the banner stays the source's short summary. It loads in the background the first time it's shown for a pack and is cached by pack id for the session (`packDescriptions`). A failure shows the error and **Retry**. A pack with no text shows "This pack has no description."
- The **Mods** tab lists the mods in the version shown (the latest, or the one picked in the version dropdown) (`FetchPackMods`, see [pack-sources.md](pack-sources.md#mod-lists)):
  - It loads in the background the first time the tab is shown for a pack version, once the version lookup has finished. Lists are cached per pack id and version (`packMods`, keyed `<pack id>@<sourceVersionId>`) for the session. CurseForge waits for a **Load mod list** click, because reading its manifest downloads the pack archive (which a later install reuses).
  - The tab title shows the count once loaded, e.g. **Mods (51)**. A filter box matches names and summaries.
  - Each mod shows its name (a link to its page when known), its version or file name, **Client only**, **Server only** and **Optional** tags, and a dimmed summary.
  - Failures show the error and a **Retry** button. The source's note (e.g. "Only mods hosted on Modrinth are listed.") appears above the list.

If nothing is selected, the pane shows "No modpacks match your search."

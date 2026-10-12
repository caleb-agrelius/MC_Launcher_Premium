# Discover Page

Browse modpacks and start downloads and installs. UI code is the `Page::Discover` branch in `src/main.cpp`.

## Sub-pages
- Discover has one sub-page per source, listed under **Discover** in the sidebar (`Widgets::SubNavItem`): **Modrinth**, **CurseForge**, **FTB**, **Technic** and **ATLauncher** (see [pack-sources.md](pack-sources.md)).
- The page title is "Discover on <source>".
- Each sub-page keeps its own state in a `SourceView`: search text, filters, results, selection, and search status. Switching sub-pages and back keeps a search.
- A sub-page's first search runs the first time it is shown.
- The last sub-page used is saved as `discoverSource` in `settings.json` and reopened at startup.

## Search
- A full-width, taller text box of up to 127 characters, with the source's hint (e.g. "Search Modrinth modpacks...").
- Results come live from the selected source. Typing schedules a search that runs once the text has been unchanged for 0.4 s (`kSearchDelaySeconds`). An empty search lists the source's default list (Modrinth: most downloaded).
- **Filters** sit on one row under the box, showing only those the source supports (see [pack-sources.md](pack-sources.md)):
  - a **Minecraft version** text box (searches after typing pauses)
  - a **loader** combo (Any loader, Forge, NeoForge, Fabric, Quilt)
  - a **Sort** combo

  Changing a combo searches immediately.
- When a source isn't available (CurseForge without an API key), the search box and filters are disabled and the status line explains why. No search is sent.
- A status line follows the filters, or wraps to its own line when it doesn't fit. Errors wrap. It shows one of:
  - a spinner with "Searching <source>..."
  - the total match count ("18.8K modpacks on Modrinth")
  - a red error. For Modrinth this is "Modrinth unavailable (...) - showing local packs".
- **Offline fallback (Modrinth only):** if the search fails, the list shows packs from `resources/index.json` whose name or description contains the query (case-insensitive). See [modpack-index.md](modpack-index.md).
- Each search shows up to 30 results (`kSearchLimit`). There is no paging yet.

## Pack list (left, 340px, scaled)
- The heading is the source's browse label (Modrinth: "MOST DOWNLOADED") when there's no query, filter or sort, and "RESULTS" otherwise.
- Each pack is a row (`Widgets::PackRow`) with its icon (see [images.md](images.md)), its name in bold (shortened with "..." if too long), and a dimmed line:
  - Online packs: newest supported Minecraft version and download count (e.g. `26.3 · 18.4M downloads`), whichever the source provides, else the author, else the source name (Technic search results until the pack is selected)
  - Local packs: version and file count
- The selected row has a rounded highlight with a green bar inside its left edge (see [ui-theme.md](ui-theme.md)). Rows scrolled out of view don't request their icons.
- Clicking a row selects it. The first result is selected after each search. "No modpacks found." appears when a search returns nothing.

## Pack details (right)
- **Hero banner** (`Widgets::HeroBanner`), sized to about a third of the pane width (200–340px, scaled):
  - Background, bottom to top: the pack's Modrinth color with a faint Minecraft-style block pattern; the small gallery thumbnail from the search result; the full-size gallery image once it has loaded. Each image fades in, and is scaled to fill and cropped.
  - When a Modrinth pack is selected for the first time (other sources don't do this), a background lookup (`FetchModrinthBanner`) finds the full-size image. If it fails, the thumbnail stays and the lookup isn't retried.
  - A gradient into the panel color keeps the text readable. On top are the pack icon, the name in large bold text, "by <author>", and chips for the Minecraft version, the loaders, and the pack version.
- Below the banner, online packs show a downloads chip (when the count is known) and a link to the pack's page (`websiteUrl`, e.g. `modrinth.com/modpack/<slug>`) that opens in the browser (`ImGui::TextLinkOpenURL`). Then comes the wrapped description.
- **Version lookup:** search results don't include files. When an online pack is selected, its latest version is fetched in the background with `FetchPackVersion` (a spinner with "Loading latest version..."). For Modrinth that fills in the version number, loaders, and the `.mrpack` file. On failure, a red "Couldn't load versions: ..." message and a **Retry** button appear.
- **Install** (sources with whole-pack installs, e.g. CurseForge) shows one card for the pack's latest version:
  - The version name, Minecraft version and loaders, then **Install pack**. The button reads **Retry** after a failure and **Reinstall** once installed.
  - Once installed (or when My Library has the same version), a green "Installed" line follows, with the game folder `installs/<pack id>/minecraft`.
  - While installing: the stage ("Downloading pack archive", "Resolving mod files", "Downloading files 120 / 312", "Extracting pack files") and a thin progress bar.
  - Clicking it runs `startPlanInstall()` in `src/main.cpp`: `BuildInstallPlan`, then `RunInstallPlan` into `installs/<pack id>/` (see [pack-sources.md](pack-sources.md)), on a worker thread. On success the pack is added to My Library.
  - Afterwards, a failed install shows "Install failed: ...". Files that must be downloaded by hand are listed with a **download page** link, and the plan's notes appear as "Note: ...".
- **Pack files** (Modrinth and local packs) shows each file as a card with its name in bold, its formatted size, and an action on the right that depends on its state:

| File state | Shown |
|---|---|
| Never downloaded | **Download** button |
| Download finished with an error | **Retry** button plus a red "Download failed: ..." message |
| Downloading | "Downloading" (with a percentage once the total size is known) plus a thin progress bar |
| Downloaded, not extracted | **Install files** button |
| Extracting | "Extracting" plus a thin progress bar |
| Extracted | "Extracted" (green) |
| Extraction failed | **Install files** again, plus a red "Extraction failed: ..." message |

- **Download** and **Retry** start a background download. See [downloads.md](downloads.md).
- **Install files** extracts the downloaded file into `installs/<pack id>/` (see [installation.md](installation.md)). When extraction succeeds, the pack is added to My Library (see [library.md](library.md)).
- A large, disabled **PLAY — COMING SOON** button sits at the bottom. See [planned.md](planned.md).

If nothing is selected, the pane shows "No modpacks match your search."

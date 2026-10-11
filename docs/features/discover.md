# Discover Page

Browse modpacks and start downloads and installs. UI code is the `Page::Discover` branch in `src/main.cpp`.

## Search
- A full-width, taller text box ("Search Modrinth modpacks...") of up to 127 characters.
- Results come live from Modrinth (see [modrinth-search.md](modrinth-search.md)). Typing schedules a search that runs once the text has been unchanged for 0.4 s (`kSearchDelaySeconds`). The first frame runs an empty search, which lists the most downloaded packs.
- A status line under the box shows a spinner with "Searching Modrinth...", the total match count, or a red "Modrinth unavailable (...) - showing local packs" message.
- **Offline fallback:** if the search fails, the list shows packs from `resources/index.json` whose name or description contains the query (case-insensitive). See [modpack-index.md](modpack-index.md).
- Each search shows up to 30 results (`kSearchLimit`). There is no paging yet.

## Pack list (left, 340px, scaled)
- The heading is "MOST DOWNLOADED" for an empty query and "RESULTS" otherwise.
- Each pack is a row (`Widgets::PackRow`) with its icon (see [images.md](images.md)), its name in bold (shortened with "..." if too long), and a dimmed line:
  - Modrinth packs: newest supported Minecraft version and download count (e.g. `26.3 · 18.4M downloads`)
  - Local packs: version and file count
- The selected row is highlighted with a green bar on its left. Rows scrolled out of view don't request their icons.
- Clicking a row selects it. The first result is selected after each search. "No modpacks found." appears when a search returns nothing.

## Pack details (right)
- **Hero banner** (`Widgets::HeroBanner`), sized to about a third of the pane width (200–340px, scaled):
  - Background, bottom to top: the pack's Modrinth color with a faint Minecraft-style block pattern; the small gallery thumbnail from the search result; the full-size gallery image once it has loaded. Each image fades in, and is scaled to fill and cropped.
  - When a Modrinth pack is selected for the first time, a background lookup (`FetchModrinthBanner`) finds the full-size image. If it fails, the thumbnail stays and the lookup isn't retried.
  - A gradient into the panel color keeps the text readable. On top are the pack icon, the name in large bold text, "by <author>", and chips for the Minecraft version, the loaders, and the pack version.
- Below the banner, Modrinth packs show a downloads chip and `modrinth.com/modpack/<slug>`. Then comes the wrapped description.
- **Version lookup:** search results don't include files. When a Modrinth pack is selected, its latest version is fetched in the background (a spinner with "Loading latest version..."). That fills in the version number, loaders, and the `.mrpack` file. On failure, a red "Couldn't load versions: ..." message and a **Retry** button appear.
- **Pack files** shows each file as a card with its name in bold, its formatted size, and an action on the right that depends on its state:

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
- **Install files** records the pack in My Library (see [library.md](library.md)) and extracts the downloaded file into `installs/<pack id>/`. See [installation.md](installation.md).
- A large, disabled **PLAY — COMING SOON** button sits at the bottom. See [planned.md](planned.md).

If nothing is selected, the pane shows "No modpacks match your search."

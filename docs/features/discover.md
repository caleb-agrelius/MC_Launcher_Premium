# Discover Page

Browse modpacks and start downloads and installs. UI code is the `Page::Discover` branch in `src/main.cpp`.

## Search
- A full-width text box ("Search Modrinth modpacks...") of up to 127 characters.
- Results come live from Modrinth (see [modrinth-search.md](modrinth-search.md)). Typing schedules a search that runs once the text has been unchanged for 0.4 s (`kSearchDelaySeconds`). The first frame runs an empty search, which lists the most downloaded packs.
- A status line under the box shows "Searching Modrinth...", the total match count, or a red "Modrinth unavailable (...) - showing local packs" message.
- **Offline fallback:** if the search fails, the list shows packs from `resources/index.json` whose name or description contains the query (case-insensitive). See [modpack-index.md](modpack-index.md).
- Each search shows up to 30 results (`kSearchLimit`). There is no paging yet.

## Pack list (left, 285px)
- The heading is "MOST DOWNLOADED" for an empty query and "RESULTS" otherwise.
- Each pack is a selectable row showing its name, then:
  - Modrinth packs: newest supported Minecraft version and download count (e.g. `26.3  |  18.4M downloads`)
  - Local packs: version and file count
- Clicking a row selects it. The first result is selected after each search. "No modpacks found." appears when a search returns nothing.

## Pack details (right)
- Shows a "MODPACK" label, the pack version, and the name.
- For Modrinth packs, a metadata line shows author, downloads, Minecraft version and loaders, followed by `modrinth.com/modpack/<slug>`.
- Then the wrapped description and a "Ready your next adventure" banner.
- **Version lookup:** search results don't include files. When a Modrinth pack is selected, its latest version is fetched in the background ("Loading latest version..."). That fills in the version number, loaders, and the `.mrpack` file. On failure, a red "Couldn't load versions: ..." message and a **Retry** button appear.
- **Pack files** lists each file with its name and formatted size, plus an action that depends on its state:

| File state | Shown |
|---|---|
| Never downloaded | **Download** button |
| Download finished with an error | **Retry** button plus a red "Download failed: ..." message |
| Downloading | "Downloading" plus a progress bar once the total size is known |
| Downloaded, not extracted | **Install files** button |
| Extracting | "Extracting" plus a progress bar |
| Extracted | "Extracted" (green) |
| Extraction failed | **Install files** again, plus a red "Extraction failed: ..." message |

- **Download** and **Retry** start a background download. See [downloads.md](downloads.md).
- **Install files** records the pack in My Library (see [library.md](library.md)) and extracts the downloaded file into `installs/<pack id>/`. See [installation.md](installation.md).
- A disabled **PLAY – COMING SOON** button sits at the bottom. See [planned.md](planned.md).

If nothing is selected, the pane shows "No modpacks match your search."

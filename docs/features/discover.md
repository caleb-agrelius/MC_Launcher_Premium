# Discover Page

Browse modpacks and start downloads and installs. UI code is the `Page::Discover` branch in `src/main.cpp`.

## Search
- A full-width text box ("Search modpacks...") of up to 127 characters.
- Shows a pack if its `name` or `description` contains the query as a substring. Matching is case-insensitive (ASCII). An empty query shows all packs.

## Pack list (left, 285px)
- Each pack is a selectable row showing its name, then its version and file count.
- Clicking a row selects it. The first pack is selected at startup.

## Pack details (right)
- Shows a "MODPACK" label, the version, the name, and the wrapped description.
- A "Ready your next adventure" banner appears below the description.
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
- **Install files** extracts the downloaded file into `installs/<pack id>/`. See [installation.md](installation.md).
- A disabled **PLAY – COMING SOON** button sits at the bottom. See [planned.md](planned.md).

If no pack is selected, the pane shows "No modpacks match your search." This message depends on the selection, not on the search results.

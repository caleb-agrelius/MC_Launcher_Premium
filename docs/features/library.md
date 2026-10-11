# My Library Page

Lists modpacks that have been extracted in this session. UI code is the `Page::Library` branch in `src/main.cpp`.

- Clicking **Install files** on Discover copies the pack into `libraryPacks` (keyed by pack id), so it stays listed after the search results change.
- A pack in `libraryPacks` appears here when `IsPackInstalled()` is true, which means at least one of its files has `installed == true` in the download state (see [downloads.md](downloads.md)).
- Each pack card shows its icon, its name, version, file count, and a note that launching isn't available yet.
- If no packs are installed, the page shows a hint to install one from Discover.
- The page has no actions: you can't open the folder, uninstall, or play from here yet.
- The library is built from in-memory state, so it is empty after a restart even if `installs/` has content.

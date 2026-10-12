# My Library Page

Lists modpacks whose files have been extracted, including ones from earlier sessions. UI code is the `Page::Library` branch in `src/main.cpp`. Saving and loading is in `src/Storage/Library.{h,cpp}`.

- When an **Install files** extraction succeeds, the install worker posts to the `UiQueue`. That task adds the pack to `libraryPacks` (a `Library`, keyed by pack id) and writes `library.json` with `SavePackLibrary`. Failed or cancelled extractions aren't added.
- At startup, `LoadPackLibrary` reads `library.json` from the data folder. Packs whose `installs/<pack id>/` folder no longer exists are dropped, and the file is rewritten on the next successful install. A missing or corrupt file gives an empty library.
- `library.json` stores each pack's id, name, description, version, author, game version, loaders, icon URL, color, website URL, source key and source ids, and files (name, URL, size). Files written before sources existed only have `fromModrinth`, which loads as the Modrinth source.
- On shutdown, the app drains the `UiQueue` after joining workers, so an extraction that finishes while closing is still saved.
- Each pack card shows its icon, its name, and a note that launching isn't available yet. Below the name is either:
  - the version and file count (Modrinth and local packs)
  - the source, version, Minecraft version and loaders, and the game folder `installs/<pack id>/minecraft` (whole-pack installs)
- Whole-pack installs add the pack when `RunInstallPlan` succeeds (see [pack-sources.md](pack-sources.md)).
- If no packs are installed, the page shows a hint to install one from Discover.
- The page has no actions: you can't open the folder, uninstall, or play from here yet.
- Packs extracted before `library.json` existed aren't detected from `installs/` alone. Install them again to list them.

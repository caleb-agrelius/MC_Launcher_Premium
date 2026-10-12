# My Library Page

Lists modpacks whose files have been extracted, including ones from earlier sessions. UI code is the `Page::Library` branch in `src/main.cpp`. Saving and loading is in `src/Storage/Library.{h,cpp}`.

- When an **Install files** extraction succeeds, the install worker posts to the `UiQueue`. That task adds the pack to `libraryPacks` (a `Library`, keyed by pack id) and writes `library.json` with `SavePackLibrary`. Failed or cancelled extractions aren't added.
- At startup, `LoadPackLibrary` reads `library.json` from the data folder. Packs whose `installs/<pack id>/` folder no longer exists are dropped, and the file is rewritten on the next successful install. A missing or corrupt file gives an empty library.
- `library.json` stores each pack's id, name, description, version, author, game version, loaders, icon URL, color, website URL, source key and source ids, and files (name, URL, size). Files written before sources existed only have `fromModrinth`, which loads as the Modrinth source.
- On shutdown, the app drains the `UiQueue` after joining workers, so an extraction that finishes while closing is still saved.
- Each pack card shows its icon, its name, and a line with its source, version, Minecraft version and loaders (when known). Whole-pack installs also show their game folder `installs/<pack id>/minecraft`. Below that is the play status: "Ready to play", the current preparation step with a spinner, "Minecraft is running", or the last error in red.
- Whole-pack installs add the pack when `RunInstallPlan` succeeds (see [pack-sources.md](pack-sources.md)).
- The card's actions sit together on the right (`AlignRightForButtons`): **Open folder** and **PLAY** when idle, **Cancel** while the pack is being prepared (with a thin progress bar when the step's progress is known), and **Stop game** while Minecraft runs. **PLAY** uses the defaults from Settings > Minecraft and Settings > Game options (see [settings.md](settings.md) and [launching.md](launching.md)). Play needs an extracted `.mrpack`, so **PLAY** is disabled for whole-pack installs (CurseForge, FTB, Technic, ATLauncher), with a tooltip saying so.
- **Open folder** opens the pack's game folder in Explorer: `instances/<pack id>/`, or `installs/<pack id>/minecraft` for whole-pack installs.
- If no packs are installed, the page shows an empty state with a **Browse modpacks** button that opens Discover.
- You can't uninstall from here yet.
- Packs extracted before `library.json` existed aren't detected from `installs/` alone. Install them again to list them.

# My Library Page

Lists modpacks whose files have been extracted, including ones from earlier sessions. UI code is the `Page::Library` branch in `src/main.cpp`. Saving and loading is in `src/Storage/Library.{h,cpp}`.

- When an **Install files** extraction succeeds, the install worker posts to the `UiQueue`. That task adds the pack to `libraryPacks` (a `Library`, keyed by pack id) and writes `library.json` with `SavePackLibrary`. Failed or cancelled extractions aren't added.
- At startup, `LoadPackLibrary` reads `library.json` from the data folder. Packs whose `installs/<pack id>/` folder no longer exists are dropped, and the file is rewritten on the next successful install. A missing or corrupt file gives an empty library.
- `library.json` stores each pack's id, name, description, version, author, game version, loaders, icon URL, color, `fromModrinth`, and files (name, URL, size).
- On shutdown, the app drains the `UiQueue` after joining workers, so an extraction that finishes while closing is still saved.
- Each pack card shows its icon, its name, and a line with its version, Minecraft version and loaders (when known). Below that is its play status: "Ready to play", the current preparation step with a spinner, "Minecraft is running", or the last error in red.
- The card's **PLAY** button starts playing (see [launching.md](launching.md)) with the defaults from Settings > Minecraft (see [settings.md](settings.md)). While the pack is being prepared it's replaced by **Cancel**, and a thin progress bar shows the step's progress when known.
- Next to **PLAY**, **Open folder** opens the pack's game folder (`instances/<pack id>/`) in Explorer. It's hidden while the pack is being prepared or running.
- If no packs are installed, the page shows an empty state with a **Browse modpacks** button that opens Discover.
- You can't uninstall from here yet.
- Packs extracted before `library.json` existed aren't detected from `installs/` alone. Install them again to list them.

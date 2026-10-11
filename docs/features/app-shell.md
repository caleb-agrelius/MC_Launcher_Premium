# App Shell

Window, rendering loop, navigation, theme, and logging.

## Startup and shutdown (`main()` in `src/main.cpp`)
1. Finds the **data folder** (`FindDataRoot`): the first of the working directory, the executable's folder, or any parent of the executable's folder that contains `resources/index.json`. The app then makes that folder the working directory, so launching `build/Debug/MC_Launcher_Premium.exe` directly still uses the repo root. If none is found, the working directory stays as-is and the pack list is empty.
2. Opens `app.log` in the data folder (append mode). It logs startup, the data folder (or that none was found), the number of local packs loaded, each Modrinth search and version lookup (result count or error), failures, and unhandled exceptions.
3. Initializes GLFW, creates a resizable 1280x760 window with an OpenGL 3.0 context (GLSL `#version 130`), and enables vsync.
4. Loads GL functions via glad, then creates the ImGui context with keyboard navigation and the GLFW + OpenGL3 backends.
5. Loads the modpack index (see [modpack-index.md](modpack-index.md)) and creates the `downloads/` and `installs/` folders.
6. Runs the frame loop until the window is closed. Each frame starts by joining finished worker threads (`ReapFinishedWorkers`), then running results posted by background threads (`UiQueue::Drain`).
7. On exit, sets `shuttingDown` so in-flight downloads and extractions cancel, joins the remaining workers, then tears down ImGui and GLFW.

## Layout
One full-window ImGui window with no title bar, split into:
- **Sidebar** (205px): app title, and selectable nav entries **Discover**, **My Library**, and **Downloads**. A footer notes that launcher integration is coming soon.
- **Main content:** a page header, then the active page.

Navigation state is the `Page` enum (`Discover`, `Library`, `Downloads`). Discover is the default.

## Pages
- Discover: [discover.md](discover.md)
- My Library: [library.md](library.md)
- Downloads: [downloads.md](downloads.md)

## Theme
`ApplyLauncherStyle()` builds on ImGui's dark style with rounded corners, roomier padding, and a dark slate background with green accents for buttons, headers, and selection. The accent text color is `ImVec4(0.40, 0.83, 0.50, 1)`. The error text color is `ImVec4(1.0, 0.42, 0.38, 1)`.

## Helpers
- `FormatFileSize(bytes)` formats sizes as B/KB/MB/GB.
- `IsPackInstalled(pack, downloads)` is true if any of the pack's files has been extracted.
- `ToLower` / `ContainsIgnoreCase` handle case-insensitive search.
- `Worker`, `StartWorker`, and `ReapFinishedWorkers` manage background threads (see [downloads.md](downloads.md)).
- `UiQueue` lets background threads hand work to the UI thread (see [modrinth-search.md](modrinth-search.md)).
- `FormatCount(n)` formats counts as 950 / 12.3K / 18.4M.

## Runtime files (gitignored, created in the data folder)
`app.log`, `imgui.ini` (ImGui layout persistence), `downloads/`, `installs/`.

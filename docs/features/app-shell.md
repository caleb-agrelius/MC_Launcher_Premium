# App Shell

Window, rendering loop, navigation, theme, and logging.

## Startup and shutdown (`main()` in `src/main.cpp`)
1. Finds the **data folder** (`FindDataRoot`): the first of the working directory, the executable's folder, or any parent of the executable's folder that contains `resources/index.json`. The app then makes that folder the working directory, so launching `build/Debug/MC_Launcher_Premium.exe` directly still uses the repo root. If none is found, the working directory stays as-is and the pack list is empty.
2. Opens `app.log` in the data folder (append mode). It logs startup, the data folder (or that none was found), the number of local packs loaded, each Modrinth search and version lookup (result count or error), failures, and unhandled exceptions.
3. Initializes GLFW and creates a resizable 1360x840 window with an OpenGL 3.0 context (GLSL `#version 130`) and vsync. `GLFW_SCALE_TO_MONITOR` makes that size logical, so the window is 1.5x larger at 150% Windows display scaling. On Windows the title bar follows the theme (`ApplyTitleBarColors`): dark or light mode on Windows 10/11, plus the theme's background and text colors on Windows 11. The minimum window size is 900x600, scaled.
4. Loads GL functions via glad, then creates the ImGui context with keyboard navigation. It loads `settings.json` (see [settings.md](settings.md)), the fonts, and the themed, scaled style (see [ui-theme.md](ui-theme.md)), colors the title bar to match the theme, then starts the GLFW + OpenGL3 backends and the `ImageCache` (see [images.md](images.md)).
5. Loads the modpack index (see [modpack-index.md](modpack-index.md)) and creates the `downloads/` and `installs/` folders.
6. Runs the frame loop until the window is closed. Each frame:
   - joins finished worker threads (`ReapFinishedWorkers`) and runs results posted by background threads (`UiQueue::Drain`)
   - rebuilds the style if the monitor DPI scale, the UI scale, or the theme changed (see [ui-theme.md](ui-theme.md)); a theme change also recolors the title bar
   - saves `settings.json` if a setting changed and no widget is being edited (so sliders and text boxes don't save on every step)
   - uploads newly decoded images (`ImageCache::Update`)
   - handles the shortcuts: Ctrl +, Ctrl -, Ctrl 0 (UI scale) and Ctrl F (go to Discover and focus the search box)
   - after the main window, draws toast notifications (`Toasts::Draw`)
7. On exit, saves `settings.json` if it has unsaved changes, sets `shuttingDown` so in-flight downloads and extractions cancel, and joins the remaining workers. It then stops the image cache (freeing its textures while the GL context still exists) and tears down ImGui and GLFW.

## Layout
One full-window ImGui window with no title bar, split into:
- **Sidebar** (230px, scaled): a grass-block logo drawn from small squares, "MC Launcher" with a "PREMIUM · JAVA" tag, the **BROWSE** entries **Discover**, **My Library**, and **Downloads**, and the **LAUNCHER** entry **Settings** (`Widgets::NavItem`; the selected one has an accent bar and bold text). Downloads shows a count badge while downloads or extractions are running. The footer holds the **UI scale** control (`-`, the current percentage, which resets to 100% when clicked, and `+`) and a "Launching: coming soon" note.
- **Main content:** a header (an accent breadcrumb such as "MINECRAFT / MODPACKS" or "LAUNCHER / SETTINGS", a large bold page title, and a subtitle), then the active page.

Navigation state is the `Page` enum (`Discover`, `Library`, `Downloads`, `Settings`). Discover is the default.

## Pages
- Discover: [discover.md](discover.md)
- My Library: [library.md](library.md)
- Downloads: [downloads.md](downloads.md)
- Settings: [settings.md](settings.md)

## Theme
Themes, fonts, and scaling live in `src/UI/Theme.{h,cpp}`. See [ui-theme.md](ui-theme.md). The GL clear color is the theme's window background.

## Helpers
- `FormatFileSize(bytes)` formats sizes as B/KB/MB/GB.
- `IsPackInstalled(pack, downloads)` is true if any of the pack's files has been extracted.
- `ToLower` / `ContainsIgnoreCase` handle case-insensitive search.
- `Worker`, `StartWorker`, and `ReapFinishedWorkers` manage background threads (see [downloads.md](downloads.md)).
- `UiQueue` lets background threads hand work to the UI thread (see [modrinth-search.md](modrinth-search.md)).
- `FormatCount(n)` formats counts as 950 / 12.3K / 18.4M.
- `NormalizeUiScale(scale)` rounds to 5% steps within 75%–200%.
- `AlignRightForButton(label)` moves the cursor so a button ends at the right edge.
- `BeginCard` / `EndCard` draw a bordered card that grows to fit its contents.
- `PackIconItem(pack, images, size)` lays out a pack icon as an item.

## Runtime files (gitignored, created in the data folder)
`app.log`, `imgui.ini` (ImGui layout persistence), `settings.json` (see [settings.md](settings.md)), `downloads/`, `installs/`.

# App Shell

Window, rendering loop, navigation, theme, and logging.

## Startup and shutdown
`main()` in `src/main.cpp` only finds the data folder, opens the log and runs `Application` (`src/App/Application.cpp`), which opens the window, builds the app (`AppRoot`, see [architecture.md](../architecture.md)), loops over frames and shuts down in order.

1. `main()` finds the **data folder** (`FindDataRoot`): the first of the working directory, the executable's folder, or any parent of the executable's folder that contains `resources/index.json`. The app then makes that folder the working directory, so launching `build/Debug/MC_Launcher_Premium.exe` directly still uses the repo root. If none is found, the working directory stays as-is and the pack list is empty.
2. `main()` opens `app.log` in the data folder (append mode). It logs startup, the data folder (or that none was found), the number of local packs loaded, each search (with its source) and version lookup (result count or error), each pack install, failures, and unhandled exceptions.
3. `Application` loads settings (`SettingsStore`), then `GlfwWindow::Create` initializes GLFW and creates a resizable OpenGL 3.0 window. Frames wait for the display in one of two ways (`GlfwWindow::UpdateDisplaySync`, re-checked every 60 frames): on the desktop compositor (`Platform::WaitForDisplayRefresh`, `DwmFlush` on Windows, with the driver's vsync off) when the compositor's refresh rate is within 5% of the rate of the monitor under the window, otherwise on the driver's vsync (`glfwSwapInterval(1)`). The driver's wait spins a CPU core; the compositor wait sleeps (a full core against about a tenth of one at 240 fps on the test PC). It falls back to the driver's vsync if the compositor wait fails. First launch uses 1360x840 scaled to the monitor DPI. Later launches restore the saved normal size, position and maximized state from `settings.window`, in screen pixels. Placements outside connected monitors' work areas fall back to the default. The window is positioned before being shown. Its title bar and taskbar icon is the logo (see Logo in [ui-theme.md](ui-theme.md)), and the title bar follows the theme on Windows. The minimum size is 900x600, scaled.
4. Loads GL functions via glad, then creates the ImGui context with keyboard navigation. `ThemeController::Initialize` loads the fonts and the themed, scaled style (see [ui-theme.md](ui-theme.md)) and colors the title bar to match the theme (`Platform::ApplyTitleBarStyle`). Then the GLFW + OpenGL3 backends start.
5. `AppRoot` builds the services and pages: the library, accounts, downloads, the `ImageCache` (see [images.md](images.md)), the modpack index (see [modpack-index.md](modpack-index.md)) and the rest. It creates the `downloads/` and `installs/` folders, restores saved downloads, and starts the shared-files cleanup.
6. Runs the frame loop until the window is closed (`AppRoot::Frame`). Each frame:
   - joins finished worker threads (`WorkerPool::Reap`) and runs results posted by background threads (`UiQueue::Drain`)
   - takes the files dropped on the window since the last frame (`GlfwWindow::TakeDroppedPaths`). The Manage page adds them to its pack (see [manage.md](manage.md#content)). On any other page, a toast says to open a pack's Manage page
   - `ThemeController::Update` refreshes the custom themes from settings (`Theme::SetCustomThemes`), then rebuilds the style if the monitor DPI scale, the UI scale, the selected theme, or a custom theme's colors changed (see [ui-theme.md](ui-theme.md)); a theme change also recolors the title bar
   - remembers the window's placement (`GlfwWindow::TrackPlacement`)
   - saves `settings.json` if a setting changed and no widget is being edited (`SettingsStore::SaveIfSettled`, so sliders and text boxes don't save on every step)
   - uploads newly decoded images (`ImageCache::Update`)
   - `Shortcuts::Process` handles Ctrl +, Ctrl -, Ctrl 0 (UI scale) and Ctrl F (go to Discover and focus the search box)
   - after the main window, draws toast notifications (`Toasts::Draw`)
   - returns whether the player did something this frame (mouse move or wheel, a key or typed character, a dropped file, an item being edited) or a toast is showing
   Between frames `Application` sleeps (`GlfwWindow::WaitEvents`, with the OS timer set to 1 ms by `Platform::UsePreciseTimer`) according to `FramePacing::Choose` (`src/App/FramePacing.*`), so the loop doesn't spin on vsync. With vsync alone a 240 Hz monitor made the idle launcher redraw 240 times a second and use a full CPU core (measured: 102% of one core, 94% of it inside the first GL call that waited for the display). The rates:
   - **Active** (input within the last second): the **Maximum frame rate** setting, or no sleep at all (the display wait above paces it, at the display's refresh rate) for **Match display**
   - **Idle** (a second without input, and **Lower frame rate when idle** on): 30 fps, never above the cap. The first event wakes the loop at once
   - **Unfocused** (with the idle option on) or **minimized** (always): 10 fps
7. On exit (`AppRoot::Shutdown`), asks running servers to save and stop, sets the shutdown flag (which cancels setup, download and sign-in work), joins remaining workers, and drains `UiQueue` to save completed work. It saves settings and the last normal window size/position and maximized state. A minimized window keeps its prior placement. It then stops the image cache and mascot, and `Application` tears down the ImGui backends, the ImGui context and the window.

**Minimum window size:** `ThemeController::ApplyAll` sets it to 1180x720 logical pixels (`Theme::S`, so it grows with Windows scaling and the UI scale; below that Discover's filter row and the sidebar's account buttons get cut off). `GlfwWindow::SetMinimumSize` caps it to the usable area (monitor minus taskbar) of the monitor under the window, so on a small screen or at a high scale the window can still be as large as the screen. A saved window smaller than the minimum is grown to it.

## Layout
One full-window ImGui window with no title bar (`AppShell::Draw`), split into:
- **Sidebar** (`Sidebar`, 230px, scaled): the interactive mob mascot beside the blocky "MC LAUNCHER" wordmark, Discover, My Library, Downloads, Servers, Settings, and account controls. Downloads shows an active-work badge. The sidebar never scrolls. Discover uses source filter chips within the page.
- **Main content:** a padded panel (24 x 18 px, scaled) with a header (an accent breadcrumb such as "MINECRAFT / MODPACKS" or "LAUNCHER / SETTINGS", a large bold page title, and a subtitle), then the active page. Switching pages or Discover sources fades the content in as it rises into place, list items bubble in after it, and the mouse wheel scrolls smoothly (see Motion in [ui-theme.md](ui-theme.md)).

Navigation state is the `Page` enum (`Discover`, `Library`, `Downloads`, `Servers`, `Settings`, `Manage`) held by `AppShell`. Pages ask to go elsewhere through `INavigator` (`ShowPage`, `ShowManage`, `ShowServer`); each page is an `IPage` that draws its own header and body. Manage and Servers are `IPackPage`s, told which pack was picked. Discover is the default page and contains one combined search with source filter chips, saved in `discoverSources`. The sidebar lists these pages without source sub-pages, and includes a random interactive mob mascot; see [ui-theme.md](ui-theme.md).

## Pages
- Discover: [discover.md](discover.md)
- My Library: [library.md](library.md)
- Downloads: [downloads.md](downloads.md)
- Servers: [servers.md](servers.md)
- Settings: [settings.md](settings.md)

## Theme
Themes, fonts, and scaling live in `src/UI/Theme.{h,cpp}`. See [ui-theme.md](ui-theme.md). The GL clear color is the theme's window background.

## Helpers
- `FormatFileSize(bytes)` formats sizes as B/KB/MB/GB.
- `ToLower` / `ContainsIgnoreCase` handle case-insensitive search.
- `WorkerPool` (`src/Core/WorkerPool.*`) runs background tasks and joins them at shutdown (see [downloads.md](downloads.md)). It catches any exception a task lets escape (logging it to stderr), so a failing task can't terminate the app. A worker frees its curl handle (`HttpClient::ReleaseThreadHandle`) before it counts as finished, so the UI thread's join never waits on curl closing connections.
- `UiQueue` (`src/Core/UiQueue.*`) lets background threads hand work to the UI thread (see [modrinth-search.md](modrinth-search.md)).
- `FormatCount(n)` formats counts as 950 / 12.3K / 18.4M.
- `Shortcuts` rounds the UI scale to 5% steps within 75%–200%.
- `UiHelpers::AlignRightForButton(label)` moves the cursor so a button ends at the right edge; `UiHelpers::PackIconItem(pack, images, size)` lays out a pack icon as an item.
- `Widgets::BeginCard` / `EndCard` draw a bordered card that grows to fit its contents.

## Runtime files (gitignored, created in the data folder)
`app.log`, `imgui.ini` (ImGui layout persistence), `settings.json` (see [settings.md](settings.md)), `library.json` (My Library, see [library.md](library.md)), `downloads.json` (finished downloads, see [downloads.md](downloads.md)), `account.dat` (see [accounts.md](accounts.md)), `minecraft/` and `instances/` (see [launching.md](launching.md)), `downloads/`, `installs/`.

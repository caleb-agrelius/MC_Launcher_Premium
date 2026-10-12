# Project layout

## Source

| Path | Contents |
| --- | --- |
| `src/main.cpp` | Program start and stop: finds the data folder, opens `app.log`, runs `Application` |
| `src/App/Application.*` | Opens the window, runs the frame loop, shuts down in order |
| `src/App/AppRoot.*` | Composition root: builds every service and page and wires them through interfaces |
| `src/App/GlfwWindow.*` | The window: GLFW, the OpenGL context, the ImGui backends, dropped files, placement |
| `src/App/ThemeController.*` | Applies the theme, UI scale and monitor DPI at startup and when they change |
| `src/Core/` | Framework-free infrastructure and interfaces: `ITaskRunner`/`WorkerPool`, `IUiPoster`/`UiQueue`, `TaskScope`, `INotifier`, `Runtime`, `IWindowControl`, `IDisplayMetrics`, `AppInfo` |
| `src/Platform/Platform.h` | Everything the launcher needs from the OS: opening folders and files, the Open dialog, memory, network addresses, the executable's folder, the title bar |
| `src/Platform/Platform_win32.cpp`, `src/Platform/Platform_posix.cpp` | The definitions for each OS; CMake builds the one that matches |
| `src/Services/AppPaths.h` | File and folder names under the data folder |
| `src/Services/Settings/` | `SettingsStore`, `JavaScanService`, `SharedFilesService` |
| `src/Services/Library/` | `PackLibrary`, `UninstallService`, `CustomPackService`, `PackContentService` |
| `src/Services/Downloads/` | `DownloadService` and `DownloadState` |
| `src/Services/Account/` | `AccountService` |
| `src/Services/Play/` | `PlayService` |
| `src/Services/Servers/` | `ServerService` and `ServerState` |
| `src/Services/Updates/` | `UpdateService` |
| `src/Services/Discover/` | `DiscoverService`, `PackDetailsService`, `OptionalModsService` and the Discover state structs |
| `src/Model/Modpack.*` | `Modpack` / `PackFile` structs, `resources/index.json` loader |
| `src/Network/IHttpClient.h` | The network as an interface (`Get`, `Post`, `DownloadToFile`) that everything else depends on |
| `src/Network/TracingHttpClient.*` | An `IHttpClient` that traces each request on the debug harness, then calls the real client |
| `src/Network/HttpClient.*` | The libcurl implementation: downloads, GET/POST, headers, cancellation |
| `src/Network/PackSources.*` | Single entry point for search, versions, and installs across sources |
| `src/Network/Modrinth.*` | Modrinth API client |
| `src/Network/CurseForge.*` | CurseForge API client and API key handling |
| `src/Network/ModSearch.*` | Modrinth mods, resource packs and shader packs for Manage > Content > Browse: search, fitting versions, installing with dependencies |
| `src/Network/Ftb.*` | FTB API client |
| `src/Network/Technic.*` | Technic Platform and Solder client |
| `src/Network/AtLauncher.*` | ATLauncher CDN client |
| `src/Network/MicrosoftAuth.*` | Microsoft, Xbox Live, and Minecraft sign-in |
| `src/Storage/Installer.*` | Zip extraction |
| `src/Storage/InstallPlan.*` | Whole-pack installs: download, verify, merge overrides |
| `src/Storage/Settings.*` | `settings.json` |
| `src/Storage/Library.*` | `library.json` and `downloads.json` |
| `src/Storage/Account.*` | `account.dat` |
| `src/Game/GameFiles.*` | Minecraft versions, libraries, assets, Java runtimes |
| `src/Game/Loaders.*` | Fabric, Quilt, Forge, and NeoForge installation |
| `src/Game/PackInstance.*` | `.mrpack` index to the version's game folder (`instances/<pack>@<version>/`) |
| `src/Game/JavaFinder.*` | Finding installed Javas (registry, common folders, Minecraft/Prism runtimes) and picking one per Minecraft version |
| `src/Game/InstanceContent.*` | Mods, resource packs and shader packs in a game or server folder: listing, on/off, removing, jar metadata |
| `src/Game/GameOptions.*` | `options.txt` defaults applied to every pack |
| `src/Game/PackCreator.*` | New packs: Minecraft and loader version lists, pack ids, the new pack's `pack.json` |
| `src/Game/Launcher.*` | Play: preparation, command line, process launch |
| `src/Game/ServerSetup.*` | Dedicated server setup, properties, EULA and launch commands |
| `src/UI/Theme.*` | Color themes, fonts, DPI and UI scaling |
| `src/UI/Widgets.*` | Shared widgets and animations |
| `src/UI/Logo.*` | The logo's pixels and wordmark font |
| `src/UI/LogView.*` | Selectable, copyable log and console text |
| `src/UI/BackgroundTasks.h` | How the Manage page and New pack dialog run slow work on a worker and post results back to the UI thread |
| `src/UI/Pages/Manage/` | A pack's Manage page: `ManagePage` plus one class per tab and card; `ManagePageHost` connects it to the app |
| `src/UI/NewPackDialog.*` | My Library > New pack... (the view; `NewPackDialogHost` creates the pack) |
| `src/UI/Pages/Settings/` | Settings page: `SettingsPage` plus one class per tab; `SettingsPageHost` connects it to the app |
| `src/UI/Navigation.h` | `Page` and `INavigator` |
| `src/UI/Common/` | `UiHelpers` (right-aligned buttons, pack icon, page header, busy and empty-state labels, source colors), `RunInBackground` (Background.h) and `VersionPicker` |
| `src/UI/Shell/` | `AppShell` (sidebar, current page, dialogs), `Sidebar`, `AccountPanel`, `Mascot`, `Shortcuts`, `NavigatorProxy`, `ToastNotifier` |
| `src/UI/Pages/` | `IPage` and one folder per page: `Discover/` (filter bar, result list, details and its parts), `Library/`, `Downloads/`, `Servers/` (list, details, Run, Settings, Mods and Docker tabs, console), `Manage/`, `Settings/` |
| `src/UI/Dialogs/` | `IDialogs`, `DialogHost` and one class per dialog (stop game, launch settings, new pack, optional mods, delete server, uninstall, update) |
| `src/UI/Toasts.*` | Toast notifications |
| `src/UI/ImageCache.*` | Asynchronous icon and banner loading (PNG, JPEG, GIF, WebP) |
| `src/UI/MobModel.*`, `src/UI/Mobs.*` | Sidebar mob rendering and randomly selected box-built mobs |
| `src/Util/DebugTrace.*` | The debug harness: channels, trace lines, secret hiding and the `app.log` tee (see [debug-harness.md](features/debug-harness.md)) |
| `src/Util/TracedFs.*` | File operations that change the disk, each traced on the harness's file channel |
| `src/Util/Hash.*`, `src/Util/Sha1.*` | SHA-1 and MD5 |
| `src/Util/Paths.*` | Long (260+ character) Windows paths |
| `src/Util/Process.*` | Process launching and interactive server console |
| `src/Util/Text.*` | Markdown/HTML descriptions to plain text, shortening, compact counts (`FormatCount`), file sizes (`FormatFileSize`), `ToLower` |
| `src/Util/ZipArchive.h` | `ZipReader` and `ZipWriter`: RAII wrappers over miniz so a zip handle is always closed |
| `src/Util/PathSafety.h` | Checks that a path from an archive stays inside the folder it is extracted to |
| `src/Util/Json.h` | `StringOrEmpty`: a JSON field as a string, or empty when it's missing or not a string |
| `src/assets/` | `minecraft_fox.glb` |
| `resources/index.json` | Offline fallback pack list |
| `resources/logo.ico`, `resources/logo.png`, `resources/app.rc` | The logo as the executable's icon, written by `logo_export` |
| `tools/LogoExport.cpp` | `logo_export`: writes `resources/logo.ico` and `resources/logo.png` |
| `docs/architecture.md` | Layers, interfaces, threads and lifetimes |
| `docs/features/` | Feature documentation, indexed in `CLAUDE.md` |
| `tests/` | Regression checks, opt-in live API checks (`LiveChecks.cpp`), and Windows window smoke test |
| `mcdebug.cmd` | Build and run helper (see the [README](../README.md#mcdebug)) |

## Data folder

The app runs from the repository root (the folder containing `resources\index.json`) and creates the following there. All are gitignored.

| Path | Contents |
| --- | --- |
| `app.log` | Startup information, searches, version lookups, installs, errors |
| `imgui.ini` | Window layout |
| `settings.json` | Settings (see [features/settings.md](features/settings.md)) |
| `library.json` | Packs listed in My Library |
| `downloads.json` | Finished downloads, restored on startup |
| `account.dat` | Signed-in account, encrypted with Windows DPAPI |
| `downloads\<pack>\` | Downloaded `.mrpack` files and pack archives |
| `installs\<pack>\` | Extracted pack files; whole-pack installs use `minecraft\` and `pack.json` |
| `minecraft\` | Shared Minecraft versions, libraries, assets, and Java runtimes |
| `instances\<pack>\` | Game directory for each played pack (mods, config, saves, logs) |
| `servers\<pack>\` | Dedicated server files, world, configuration and console logs |

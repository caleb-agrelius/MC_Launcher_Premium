# MC_Launcher_Premium

A C++ Dear ImGui desktop app (GLFW + OpenGL3) for browsing, downloading, and extracting Minecraft Java modpacks, on Windows and Linux. Dependencies come from vcpkg: glfw3, glad, imgui, nlohmann-json, curl, miniz, libwebp, and stb. Build with CMake (see README.md).

Other docs: [docs/development.md](docs/development.md) (debugging, logs, troubleshooting) and [docs/project-layout.md](docs/project-layout.md) (source files, data folder).

## Feature docs

`docs/features/` describes what the launcher currently does, with one file per area. **Read a doc only when your task needs information about that area.** Don't load them all up front.

| Doc | Read when working on |
|---|---|
| [architecture.md](docs/architecture.md) | Where code belongs: layers, interfaces, threads and lifetimes, adding a service or page |
| [debug-harness.md](docs/features/debug-harness.md) | Console tracing of API requests, file operations, processes and `app.log` (`mcdebug debug`, `--debug`, `MCLP_DEBUG`, `src/Util/DebugTrace`, `TracedFs`, `TracingHttpClient`) |
| [app-shell.md](docs/features/app-shell.md) | Startup/shutdown, window, render loop, sidebar navigation, logging |
| [ui-theme.md](docs/features/ui-theme.md) | Color themes, fonts, DPI and UI scaling, shared widgets, toasts (`src/UI/Theme`, `src/UI/Widgets`, `src/UI/Toasts`) |
| [settings.md](docs/features/settings.md) | Settings page, `settings.json` fields, Minecraft launch defaults (`src/UI/Pages/Settings`, `src/Storage/Settings`) |
| [images.md](docs/features/images.md) | Pack icons and banners: `ImageCache` (download, decode, GL textures) |
| [modpack-index.md](docs/features/modpack-index.md) | `resources/index.json` (offline fallback), `LoadIndex`, `Modpack`/`PackFile` structs |
| [pack-sources.md](docs/features/pack-sources.md) | Source dispatcher (`PackSources`), search filters, Discover sub-pages, install plans (`InstallPlan`, `RunInstallPlan`) |
| [curseforge.md](docs/features/curseforge.md) | CurseForge client, API key setup (Settings `curseForgeApiKey`, `MCLP_CURSEFORGE_API_KEY`), blocked files, Modrinth hash fallback, mods/resource packs/shaders in Manage > Content > Browse |
| [ftb.md](docs/features/ftb.md) | FTB client: in-memory search over all packs, version manifests, one-at-a-time downloads |
| [technic.md](docs/features/technic.md) | Technic client: `build` parameter, Solder vs single-zip packs, local filters |
| [atlauncher.md](docs/features/atlauncher.md) | ATLauncher client: CDN pack list, `Configs.json`, mod types and folders |
| [modrinth-search.md](docs/features/modrinth-search.md) | Modrinth API client, `HttpClient::Get`, search/version lookup threading (`UiQueue`) |
| [request-caching.md](docs/features/request-caching.md) | `CachingHttpClient`: which GETs are remembered and for how long, what stays uncached, Discover's Refresh button |
| [discover.md](docs/features/discover.md) | Discover page: search, pack list, details pane, per-file action buttons |
| [downloads.md](docs/features/downloads.md) | `HttpClient`, download state map and threading, Downloads page |
| [shared-files.md](docs/features/shared-files.md) | Sharing identical mod files between packs (hard links, `minecraft/shared-files/`, `SharedFiles`) |
| [installation.md](docs/features/installation.md) | Zip extraction (`InstallZip`), the `installs/` layout |
| [library.md](docs/features/library.md) | My Library page |
| [manage.md](docs/features/manage.md) | A pack's Manage page: overview, content (mods, resource and shader packs), changelog, logs, screenshots (`src/UI/Pages/Manage`, `src/Game/InstanceContent`) |
| [servers.md](docs/features/servers.md) | Dedicated server setup, settings, EULA, process lifecycle and console |
| [platforms.md](docs/features/platforms.md) | What differs between Windows and Linux (process layer, sign-in storage, Java search, fonts, data folder), `CMakePresets.json`, the Linux build and CI |
| [launching.md](docs/features/launching.md) | PLAY: `.mrpack` instances, Minecraft/Java/loader install, version JSON, launch command |
| [accounts.md](docs/features/accounts.md) | Microsoft sign-in, the account check before Play, `account.dat` |
| [planned.md](docs/features/planned.md) | Features that aren't implemented yet |

## Offline launches

Launch the app offline (`mcdebug offline`, `--offline` or `MCLP_OFFLINE=1`) for any run or test that doesn't depend on the network or a third-party API: UI, layout, settings, Library, Manage, the UI test harness. It refuses every request instantly instead of calling Modrinth, CurseForge, FTB, Technic and ATLauncher on each launch. Go online only when the work touches an API client, search, version lists, images, sign-in or downloads. See [debug-harness.md](docs/features/debug-harness.md#offline-mode).

## Keeping docs current

When a change adds, removes, or changes user-visible behavior or a feature's internals, update the matching doc in the same change. Add a new doc and table row for a new area. Docs describe current behavior only. Put future work in `planned.md`.

## Code style

Follow the C++ Core Guidelines.


## Reusable logic and views

Build each piece of logic or UI once and share it. Before writing a widget, helper or rule, look for one that already does the job (`src/UI/Widgets`, `UI/Common`, `Util`, the services) and extend it. When two views need the same thing, extract it and call it from both rather than copying: for example the source-tag pill in `Widgets.cpp` serves the list row and the grid card, `DiscoverResultList::Describe()` feeds both result views, and `DiscoverService::ToggleSource` is the one place the source-filter rules live.

- Put rules and state changes in a service or model, not in a draw function, so every view of the data behaves the same and the rule can be tested without ImGui.
- A view that comes in variants (list and grid, normal and compact) takes the variant as a parameter or splits its sections into small methods. It does not get a second copy of the whole function.
- Keep per-frame work cheap: skip drawing and image requests for items that are not visible (`ImGui::IsItemVisible`), do not rebuild lists every frame, and do not do I/O on the UI thread.

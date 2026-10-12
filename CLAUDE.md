# MC_Launcher_Premium

A C++ Dear ImGui desktop app (GLFW + OpenGL3) for browsing, downloading, and extracting Minecraft Java modpacks. Dependencies come from vcpkg: glfw3, glad, imgui, nlohmann-json, curl, miniz, libwebp, and stb. Build with CMake (see README.md).

## Feature docs

`docs/features/` describes what the launcher currently does, with one file per area. **Read a doc only when your task needs information about that area.** Don't load them all up front.

| Doc | Read when working on |
|---|---|
| [app-shell.md](docs/features/app-shell.md) | Startup/shutdown, window, render loop, sidebar navigation, logging |
| [ui-theme.md](docs/features/ui-theme.md) | Color themes, fonts, DPI and UI scaling, shared widgets, toasts (`src/UI/Theme`, `src/UI/Widgets`, `src/UI/Toasts`) |
| [settings.md](docs/features/settings.md) | Settings page, `settings.json` fields, Minecraft launch defaults (`src/UI/SettingsPage`, `src/Storage/Settings`) |
| [images.md](docs/features/images.md) | Pack icons and banners: `ImageCache` (download, decode, GL textures) |
| [modpack-index.md](docs/features/modpack-index.md) | `resources/index.json` (offline fallback), `LoadIndex`, `Modpack`/`PackFile` structs |
| [pack-sources.md](docs/features/pack-sources.md) | Source dispatcher (`PackSources`), search filters, Discover sub-pages, install plans (`InstallPlan`, `RunInstallPlan`) |
| [curseforge.md](docs/features/curseforge.md) | CurseForge client, API key setup (Settings `curseForgeApiKey`, `MCLP_CURSEFORGE_API_KEY`), blocked files, Modrinth hash fallback |
| [ftb.md](docs/features/ftb.md) | FTB client: in-memory search over all packs, version manifests, one-at-a-time downloads |
| [technic.md](docs/features/technic.md) | Technic client: `build` parameter, Solder vs single-zip packs, local filters |
| [atlauncher.md](docs/features/atlauncher.md) | ATLauncher client: CDN pack list, `Configs.json`, mod types and folders |
| [modrinth-search.md](docs/features/modrinth-search.md) | Modrinth API client, `HttpClient::Get`, search/version lookup threading (`UiQueue`) |
| [discover.md](docs/features/discover.md) | Discover page: search, pack list, details pane, per-file action buttons |
| [downloads.md](docs/features/downloads.md) | `HttpClient`, download state map and threading, Downloads page |
| [installation.md](docs/features/installation.md) | Zip extraction (`InstallZip`), the `installs/` layout |
| [library.md](docs/features/library.md) | My Library page |
| [launching.md](docs/features/launching.md) | PLAY: `.mrpack` instances, Minecraft/Java/loader install, version JSON, launch command |
| [accounts.md](docs/features/accounts.md) | Microsoft sign-in, offline mode, `account.dat` |
| [planned.md](docs/features/planned.md) | Features that aren't implemented yet |

## Keeping docs current

When a change adds, removes, or changes user-visible behavior or a feature's internals, update the matching doc in the same change. Add a new doc and table row for a new area. Docs describe current behavior only. Put future work in `planned.md`.

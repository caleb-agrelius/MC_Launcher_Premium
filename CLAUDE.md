# MC_Launcher_Premium

A C++ Dear ImGui desktop app (GLFW + OpenGL3) for browsing, downloading, and extracting Minecraft Java modpacks. Dependencies come from vcpkg: glfw3, glad, imgui, nlohmann-json, curl, and miniz. Build with CMake (see README.md).

## Feature docs

`docs/features/` describes what the launcher currently does, with one file per area. **Read a doc only when your task needs information about that area.** Don't load them all up front.

| Doc | Read when working on |
|---|---|
| [app-shell.md](docs/features/app-shell.md) | Startup/shutdown, window, render loop, sidebar navigation, theme, logging |
| [modpack-index.md](docs/features/modpack-index.md) | `resources/index.json` (offline fallback), `LoadIndex`, `Modpack`/`PackFile` structs |
| [modrinth-search.md](docs/features/modrinth-search.md) | Modrinth API client, `HttpClient::Get`, search/version lookup threading (`UiQueue`) |
| [discover.md](docs/features/discover.md) | Discover page: search, pack list, details pane, per-file action buttons |
| [downloads.md](docs/features/downloads.md) | `HttpClient`, download state map and threading, Downloads page |
| [installation.md](docs/features/installation.md) | Zip extraction (`InstallZip`), the `installs/` layout |
| [library.md](docs/features/library.md) | My Library page |
| [planned.md](docs/features/planned.md) | Features that aren't implemented yet (play, Minecraft download, sign-in) |

## Keeping docs current

When a change adds, removes, or changes user-visible behavior or a feature's internals, update the matching doc in the same change. Add a new doc and table row for a new area. Docs describe current behavior only. Put future work in `planned.md`.

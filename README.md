# MC_Launcher_Premium

A Minecraft: Java Edition modpack launcher for Windows, written in C++ with Dear ImGui (GLFW + OpenGL 3).

## Contributors

- Caleb Agrelius
- Elijah Gibson
- Michael Fisher
- Benjamin Fisher

## Features

- Search Modrinth, CurseForge, FTB, Technic, and ATLauncher together, with source, Minecraft version, loader, and sort filters. CurseForge requires an API key (see [CurseForge API key](#curseforge-api-key)). Modrinth search falls back to `resources/index.json` when offline.
- Download and extract Modrinth `.mrpack` files.
- Install complete packs from CurseForge, FTB, Technic, and ATLauncher, with hash-checked mod downloads.
- Play Modrinth packs: installs Minecraft, a Mojang Java runtime, and the mod loader (Fabric, Quilt, Forge, or NeoForge), then launches the game. Packs from other sources cannot be played yet.
- Microsoft account sign-in or offline play. Microsoft sign-in requires an Entra app client ID approved for the Minecraft API; see [docs/features/account-sign-in.md](docs/features/account-sign-in.md).
- Color themes, UI scaling, launch and game-option defaults on the Settings page.
- Host installed Modrinth packs from **Servers** or My Library's **Server** button, with setup, settings and a built-in console. See [server controls](docs/features/servers.md).
- Consistent Library and Downloads cards, plus window size, position and maximized state remembered across closes.
- A random interactive Minecraft mob mascot in the sidebar. Click to hop; right-click for another mob.

The sidebar fox model is by [Aarols32](https://sketchfab.com/3d-models/minecraft-fox-1bb01070f46b430695f96c0d7fb58aec), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

Feature documentation is in [docs/features/](docs/features/). Planned work is listed in [docs/features/planned.md](docs/features/planned.md).

## Prerequisites

| Tool | Notes |
| --- | --- |
| [Visual Studio 2022](https://visualstudio.microsoft.com/) | **Desktop development with C++** workload (includes MSVC and CMake) |
| [vcpkg](https://github.com/microsoft/vcpkg) | Optional if you use `mcdebug`, which falls back to the copy bundled with Visual Studio |
| Git | Used by vcpkg and by CMake to fetch the ImGui backends |

To install vcpkg separately:

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

Dependencies are listed in `vcpkg.json` and are installed automatically when CMake configures the project.

## Build

Run from the repository root in **Developer PowerShell for VS 2022**. Replace `C:/vcpkg` with your vcpkg path.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug      # build\Debug\MC_Launcher_Premium.exe
cmake --build build --config Release    # build\Release\MC_Launcher_Premium.exe
```

Re-run the configure command after adding source files or editing `CMakeLists.txt`.

## CI and releases

GitHub Actions (`.github/workflows/`):

- **CI** (`ci.yml`) runs on every pull request: it configures with vcpkg, builds the app and `launcher_checks` (not `live_checks` or `logo_export`), and runs the checks through CTest (offline). It builds Debug only (about four times faster than Release on the runner), and merging to `main` does not trigger another build.
- **Release** (`release.yml`) is manual: Actions > Release > Run workflow, enter a version such as `1.2.0`. It builds Release from the selected ref, runs the checks, and publishes `MC_Launcher_Premium-v<version>-win64.zip` (exe, DLLs, `resources/`) as GitHub release `v<version>`.
- Both call the reusable `build.yml`.

## CurseForge API key

CurseForge requires each application to use its own API key. Without one, searches skip CurseForge; other sources are unaffected.

1. Request a key in the [CurseForge for Studios console](https://console.curseforge.com/) and accept the [third-party API terms](https://support.curseforge.com/en/support/solutions/articles/9000207405-curse-forge-3rd-party-api-terms-and-conditions).
2. Provide the key in one of two ways. Do not commit it.
   - **Settings > Downloads > CurseForge > API key.** Saved in `settings.json` (gitignored) and applied immediately. Takes precedence over the environment variable.
   - **`MCLP_CURSEFORGE_API_KEY` environment variable.** Used when the Settings field is empty.

     ```powershell
     $env:MCLP_CURSEFORGE_API_KEY = '<your key>'      # current session; single quotes because keys can contain '$'
     setx MCLP_CURSEFORGE_API_KEY "<your key>"        # persistent; applies to new terminals
     ```

## Run

```powershell
.\build\Debug\MC_Launcher_Premium.exe
```

The executable locates the repository by searching upward from its own folder for `resources\index.json`, so it can be started from any directory. Runtime files (`app.log`, `settings.json`, `downloads\`, `installs\`, and others) are created in the repository root and are gitignored. See [docs/project-layout.md](docs/project-layout.md#data-folder).

### mcdebug

`mcdebug.cmd` in the repository root configures, builds, and runs the Debug build.

| Command | Action |
| --- | --- |
| `mcdebug build` | Configure and build the Debug executable (`mcdebug build all` also builds `launcher_checks` and `live_checks`) |
| `mcdebug run` | Build, then start the executable from the repository root |
| `mcdebug debug [channels]` | Build, then run it in this console with the [debug harness](docs/features/debug-harness.md) on: API requests, file operations, processes and `app.log` lines print as they happen, e.g. `mcdebug debug http,file` |
| `mcdebug help` | List commands |

- CMake is taken from `PATH`, or from the Visual Studio installation if it is not on `PATH`.
- vcpkg is taken from `VCPKG_ROOT` if set, otherwise from the Visual Studio installation.
- In VS Code, open the repository as a folder; the workspace settings add the repository root to the integrated terminal's `PATH`, so `mcdebug` works from any subfolder. Elsewhere, run it from the repository root.

Debugging, logs, and troubleshooting are covered in [docs/development.md](docs/development.md).

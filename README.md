Development Team:

- Caleb Agrelius
- Elijah Gibson
- Michael Fisher
- Benjamin Fisher

# MC_Launcher_Premium

A Minecraft Java Edition modpack launcher built with C++ and Dear ImGui.

## Features

- Browse modpacks from several sources, each on its own Discover sub-page with search filters (Minecraft version, loader, sort):
  - [Modrinth](https://modrinth.com/modpacks), no API key needed. The app falls back to `resources/index.json` when offline.
  - [CurseForge](https://www.curseforge.com/minecraft/search?class=modpacks), which needs an API key (see [CurseForge API key](#curseforge-api-key)).
  - [FTB](https://www.feed-the-beast.com/modpacks), no key needed.
  - [Technic](https://www.technicpack.net/modpacks), no key needed.
  - [ATLauncher](https://atlauncher.com/packs), no key needed.
- Download a Modrinth pack's `.mrpack` with progress, then extract it into `installs/<pack>/`, with protection against malicious archive paths.
- Install a whole pack from the other sources in one step. Every mod file is downloaded and hash-checked into `installs/<pack>/minecraft/`.
- Play a Modrinth pack: the launcher downloads the pack's mods, Minecraft, a Mojang Java runtime and the mod loader (Fabric, Quilt, Forge or NeoForge), then starts the game. See [docs/features/launching.md](docs/features/launching.md). Packs from the other sources can't be played yet.
- Sign in with a Microsoft account, or play offline with a username. See [docs/features/accounts.md](docs/features/accounts.md).
- An interactive 3D Minecraft fox mascot in the sidebar (automatic cursor tracking and click-to-hop).

The fox model is by [Aarols32](https://sketchfab.com/3d-models/minecraft-fox-1bb01070f46b430695f96c0d7fb58aec), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

Microsoft sign-in uses a default Entra app client ID, editable in Settings > Account, and requires approval for the Minecraft API. See the [sign-in setup guide](docs/features/account-sign-in.md) and [planned features](docs/features/planned.md).

## Prerequisites (Windows)

| Tool                                                      | Notes                                                                              |
| --------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| [Visual Studio 2022](https://visualstudio.microsoft.com/) | Install the **Desktop development with C++** workload. It includes MSVC and CMake. |
| [vcpkg](https://github.com/microsoft/vcpkg)               | Supplies the C++ libraries                                                         |
| Git                                                       | Used by vcpkg, and by CMake to fetch the ImGui backends                            |

Run the commands below in **Developer PowerShell for VS 2022** (Start menu), which puts `cmake` on your PATH. Replace `C:\vcpkg` with wherever you cloned vcpkg.

### 1. Install vcpkg and the libraries (one-time)

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install glfw3 glad imgui nlohmann-json curl miniz libwebp stb --triplet x64-windows
```

## Build

From the repo root:

```powershell
# Configure (once, or after adding source files / changing CMakeLists.txt)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Debug      # -> build\Debug\MC_Launcher_Premium.exe
cmake --build build --config Release    # -> build\Release\MC_Launcher_Premium.exe
```

- The first configure downloads the ImGui GLFW/OpenGL backend files from GitHub.
- The build copies the required DLLs (glfw3, libcurl, zlib, miniz) next to the exe.
- To enable Microsoft sign-in, set `"microsoftClientId"` in `settings.json` to the Application (client) ID from your Entra app registration. The app also needs approval for the Minecraft API; see [docs/features/accounts.md](docs/features/accounts.md).

### CurseForge API key

CurseForge requires every app to use its own API key. Without one, the CurseForge sub-page is disabled; the other sources still work.

1. Apply for a key in the [CurseForge for Studios console](https://console.curseforge.com/) and accept the [3rd-party API terms](https://support.curseforge.com/en/support/solutions/articles/9000207405-curse-forge-3rd-party-api-terms-and-conditions). Don't reuse another launcher's key.
2. Give the app the key in one of two ways. **Never commit the key.**
   - **In the app:** paste it into **Settings > Downloads > CurseForge > API key**. It's saved in `settings.json` in the data folder (gitignored) and used right away. A key set here wins over the environment variable.
   - **Environment variable:** set `MCLP_CURSEFORGE_API_KEY`. It's used when the Settings field is empty.

     ```powershell
     $env:MCLP_CURSEFORGE_API_KEY = '<your key>'   # single quotes: keys contain '$'
     .\build\Debug\MC_Launcher_Premium.exe
     ```

     To keep it across sessions, use `setx MCLP_CURSEFORGE_API_KEY "<your key>"` and open a new terminal.

> The `MC_Launcher_Premium.slnx` / `.vcxproj` in the repo root don't build as-is because they expect ImGui backend files that aren't in the repo. Use the CMake build, or the solution CMake generates (see below).

## Run

```powershell
.\build\Debug\MC_Launcher_Premium.exe
```

You can also double-click the exe or start it from any folder. The app finds the repo by searching upward from the exe for `resources\index.json`, and creates these in the repo root:

| Path                | Contents                                                       |
| ------------------- | -------------------------------------------------------------- |
| `app.log`           | Startup info, packs loaded, each search/version lookup, errors |
| `downloads\<pack>\` | Downloaded `.mrpack` files and pack archives                   |
| `installs\<pack>\`  | Extracted pack files; whole-pack installs use `minecraft\` + `pack.json` |
| `imgui.ini`         | Window layout                                                  |
| `settings.json`     | Theme, UI scale, launch defaults, last Discover sub-page, and other settings |
| `library.json`      | Packs listed in My Library                                     |
| `downloads.json`    | Finished downloads, restored on startup                        |
| `account.dat`       | Signed-in account (encrypted with Windows DPAPI)               |
| `minecraft\`        | Shared Minecraft versions, libraries, assets, Java runtimes    |
| `instances\<pack>\` | Each pack's game folder (mods, config, saves, logs)            |

All of these are gitignored.

## Debug

### Visual Studio

1. Configure once (see [Build](#build)), then open **`build\MC_Launcher_Premium.sln`**.
2. Press **F5**. `MC_Launcher_Premium` is already the startup project, and it runs from the repo root.
3. Set breakpoints as usual. Useful places:

   | Area           | Code                                                   |
   | -------------- | ------------------------------------------------------ |
   | Search results | `SearchPacks` in `src/Network/PackSources.cpp` (dispatches to each source) |
   | Pack installs  | `BuildInstallPlan` / `RunInstallPlan` (`src/Storage/InstallPlan.cpp`) |
   | Downloads      | `HttpClient::DownloadToFile`                           |
   | Extraction     | `InstallZip` in `src/Storage/Installer.cpp`            |
   | Play           | `PlayModpack` in `src/Game/Launcher.cpp`               |
   | Sign-in        | `src/Network/MicrosoftAuth.cpp`                        |
   | UI             | the frame loop in `src/main.cpp`                       |

Background work (searches, downloads, extraction, sign-in, play) runs on worker threads. Use **Debug → Windows → Threads** to switch between them.

### VS Code

Install the **C/C++** and **CMake Tools** extensions. Build as above, then add `.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug launcher",
      "type": "cppvsdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/Debug/MC_Launcher_Premium.exe",
      "cwd": "${workspaceFolder}",
      "console": "integratedTerminal"
    }
  ]
}
```

Rebuild with `cmake --build build --config Debug` before each debug session, or wire that up as a `preLaunchTask`.

### Logs

Check `app.log` first. A healthy start looks like:

```
Starting app
Data folder: C:\path\to\MC_Launcher_Premium
Loaded 1 local modpack(s)
Search "": 30 of 18798 result(s)
Version lookup for fabulously-optimized: 14.1.1 (Fabulously.Optimized-v14.1.1.mrpack)
```

### Troubleshooting

| Symptom                                                                  | Cause / fix                                                                                                                        |
| ------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------- |
| CurseForge sub-page says it needs an API key, or "rejected the API key"  | See [CurseForge API key](#curseforge-api-key).                                                                                     |
| Pack list is empty and the status line is red                            | Modrinth couldn't be reached. The error appears in the status line and in `app.log`; the app falls back to `resources\index.json`. |
| `Could not find resources\index.json` in `app.log`                       | The exe was moved outside the repo. Run it from inside the repo tree.                                                              |
| `Could not find a package configuration file provided by "glfw3"` (etc.) | vcpkg libraries aren't installed, or `CMAKE_TOOLCHAIN_FILE` is wrong. Re-run the vcpkg install and configure steps.                |
| Errors in `imgui_impl_glfw.cpp` / `imgui_impl_opengl3.cpp`               | The vcpkg ImGui version doesn't match the backend tag in `CMakeLists.txt` (`GIT_TAG`). Make them match.                            |
| `LNK1104: cannot open file ...MC_Launcher_Premium.exe`                   | The app is still running. Close it and rebuild.                                                                                    |

## Project layout

```
src/main.cpp              Window, UI, threading
src/Model/Modpack.*       Modpack data + local index loader
src/Network/HttpClient.*  libcurl wrapper (downloads, GET/POST, headers, cancel)
src/Network/MicrosoftAuth.* Microsoft -> Xbox -> Minecraft sign-in
src/Network/PackSources.* One entry point per operation for every pack source
src/Network/Modrinth.*    Modrinth API client (search, versions)
src/Network/CurseForge.*  CurseForge API client (search, versions, install plans)
src/Network/Ftb.*         FTB API client
src/Network/Technic.*     Technic Platform + Solder client
src/Network/AtLauncher.*  ATLauncher CDN client
src/Storage/Installer.*   Zip extraction
src/Storage/InstallPlan.* Whole-pack installs: download, verify, merge overrides
src/Util/Hash.*           SHA-1 / MD5
src/Util/Paths.*          Long (260+ character) Windows paths
src/Storage/Settings.*    settings.json (theme, UI scale, launch defaults, Discover sub-page)
src/Storage/Library.*     library.json (My Library), downloads.json (finished downloads)
src/Storage/Account.*     account.dat (signed-in or offline account)
src/Game/GameFiles.*      Minecraft versions, libraries, assets, Java runtimes
src/Game/Loaders.*        Fabric, Quilt, Forge, NeoForge
src/Game/PackInstance.*   .mrpack index -> instances/<pack>/
src/Game/Launcher.*       Play: prepare everything, build the command line, run Java
src/Game/GameOptions.*    Vanilla options.txt defaults applied to every pack
src/Util/                 SHA-1, process launching
src/Platform.*            Open folders, system memory
src/UI/Theme.*            Color themes, fonts, DPI/UI scaling
src/UI/SettingsPage.*     Settings page
src/UI/Toasts.*           Toast notifications
src/UI/Widgets.*          Pack rows, hero banner, chips, toggles, theme cards
src/UI/ImageCache.*       Async icon/banner loading (PNG, JPEG, WebP)
resources/index.json      Offline fallback pack list
docs/features/            What each feature does (start with CLAUDE.md)
```

Building and running on Windows:

- Requires CMake (>=3.14), a C++17 compiler, and vcpkg.
- Set the `VCPKG_ROOT` environment variable to your vcpkg installation directory.
- Open this repository as a folder in VS Code and open a new integrated terminal. The workspace settings add the repository directory to that terminal's `PATH`.
- Run `mcdebug build` to configure and build the Debug executable.
- Run `mcdebug run` to build and launch the application.
- Run `mcdebug help` to see the available commands.

The command uses the vcpkg manifest in `vcpkg.json` to install project dependencies when CMake configures the build. Run the launcher from its repository directory so it can find `resources/index.json`. Downloads are saved to `downloads/` and extracted to `installs/<modpack-id>/` when you click Install.

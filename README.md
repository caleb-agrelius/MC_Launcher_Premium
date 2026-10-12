Development Team:

- Caleb Agrelius
- Elijah Gibson
- Michael Fisher
- Benjamin Fisher

# MC_Launcher_Premium

A Minecraft Java Edition modpack launcher built with C++ and Dear ImGui.

## Features

- Search modpacks live on [Modrinth](https://modrinth.com/modpacks), no API key needed. The app falls back to `resources/index.json` when offline.
- Download a pack's `.mrpack` with progress.
- Extract it into `installs/<pack>/`, with protection against malicious archive paths.
- Play it: the launcher downloads the pack's mods, Minecraft, a Mojang Java runtime and the mod loader (Fabric, Quilt, Forge or NeoForge), then starts the game. See [docs/features/launching.md](docs/features/launching.md).
- Sign in with a Microsoft account, or play offline with a username. See [docs/features/accounts.md](docs/features/accounts.md).

Microsoft sign-in needs an Entra app client ID set as `"microsoftClientId"` in `settings.json` and approval for the Minecraft API. See the [sign-in setup guide](docs/features/account-sign-in.md) and [planned features](docs/features/planned.md).

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

> The `MC_Launcher_Premium.slnx` / `.vcxproj` in the repo root don't build as-is because they expect ImGui backend files that aren't in the repo. Use the CMake build, or the solution CMake generates (see below).

## Run

```powershell
.\build\Debug\MC_Launcher_Premium.exe
```

You can also double-click the exe or start it from any folder. The app finds the repo by searching upward from the exe for `resources\index.json`, and creates these in the repo root:

| Path                | Contents                                                       |
| ------------------- | -------------------------------------------------------------- |
| `app.log`           | Startup info, packs loaded, each search/version lookup, errors |
| `downloads\<pack>\` | Downloaded `.mrpack` files                                     |
| `installs\<pack>\`  | Extracted pack files                                           |
| `imgui.ini`         | Window layout                                                  |
| `settings.json`     | UI scale                                                       |
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
   | Search results | `SearchModrinthModpacks` in `src/Network/Modrinth.cpp` |
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
| Pack list is empty and the status line is red                            | Modrinth couldn't be reached. The error appears in the status line and in `app.log`; the app falls back to `resources\index.json`. |
| `Could not find resources\index.json` in `app.log`                       | The exe was moved outside the repo. Run it from inside the repo tree.                                                              |
| `Could not find a package configuration file provided by "glfw3"` (etc.) | vcpkg libraries aren't installed, or `CMAKE_TOOLCHAIN_FILE` is wrong. Re-run the vcpkg install and configure steps.                |
| Errors in `imgui_impl_glfw.cpp` / `imgui_impl_opengl3.cpp`               | The vcpkg ImGui version doesn't match the backend tag in `CMakeLists.txt` (`GIT_TAG`). Make them match.                            |
| `LNK1104: cannot open file ...MC_Launcher_Premium.exe`                   | The app is still running. Close it and rebuild.                                                                                    |
| Extraction fails with `Failed to extract file`                           | The path is longer than Windows' 260-character limit. Keep the repo in a short path such as `C:\src\`.                             |

## Project layout

```
src/main.cpp              Window, UI, threading
src/Model/Modpack.*       Modpack data + local index loader
src/Network/HttpClient.*  libcurl wrapper (downloads, GET/POST, cancel)
src/Network/MicrosoftAuth.* Microsoft -> Xbox -> Minecraft sign-in
src/Network/Modrinth.*    Modrinth API client (search, versions)
src/Storage/Installer.*   Zip extraction
src/Storage/Settings.*    settings.json (UI scale)
src/Storage/Library.*     library.json (My Library), downloads.json (finished downloads)
src/Storage/Account.*     account.dat (signed-in or offline account)
src/Game/GameFiles.*      Minecraft versions, libraries, assets, Java runtimes
src/Game/Loaders.*        Fabric, Quilt, Forge, NeoForge
src/Game/PackInstance.*   .mrpack index -> instances/<pack>/
src/Game/Launcher.*       Play: prepare everything, build the command line, run Java
src/Util/                 SHA-1, process launching
src/UI/Theme.*            Colors, fonts, DPI/UI scaling
src/UI/Widgets.*          Pack rows, hero banner, chips, spinner
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

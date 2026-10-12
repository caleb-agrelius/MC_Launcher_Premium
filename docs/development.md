# Development

Debugging, logs, and troubleshooting. Build steps are in the [README](../README.md#build).

## Build speed

`mcdebug build`, `run`, `debug` and `offline` build only the app target; `mcdebug build all` also builds `launcher_checks`, `live_checks` and `logo_export` (the check programs compile most of the same sources again). MSVC compiles files in parallel (`/MP`), and `src/Pch.h` is a precompiled header of the standard library, nlohmann/json and `imgui.h` for the app and `launcher_checks`: add only headers that never change with app code, since editing anything in it rebuilds every file. A clean app build takes about 8 s and a one-file change about 1 s; project headers that many files include (`Widgets.h`, `Modpack.h`, `Settings.h`) still recompile every file that includes them, so keep new declarations out of them when a narrower header will do.

## Regression checks

`BUILD_TESTING` defaults to `ON`. After building Debug, run:

```powershell
ctest --test-dir build -C Debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tests/WindowSmoke.ps1
```

The CTest checks cover window settings, server file filtering and setup, preserved configuration, server mods, host IP validation, server deletion, the player data a pack update carries over, card heights, and console commands using local fixtures. The Windows smoke test launches the app with isolated data and checks window resize, close, reopen, maximization and minimization. Fixtures are kept under gitignored `tests/out/` folders.

## Driving the real window

`tests/UiDriver.ps1` is a set of PowerShell helpers for testing the launcher the way a person uses it: `Start-App` runs the exe in a throwaway data folder (`New-Fixture`), `Click-At`, `Wheel-At` and `Move-Mouse` send real mouse input, `Shot` saves a screenshot of the window, and `Get-Shift` measures how far a strip of the window moved between two screenshots (a card that jumps while scrolling shows up as a column that moved by a different amount). Dot-source it from a script or a PowerShell session.

The harness starts the app offline (see [debug-harness.md](features/debug-harness.md#offline-mode)); set `$script:AppOnline = $true` before `Start-App` for a test that needs the network.

`tests/UiSmoke.ps1` uses it to check the Settings page offline: the tip's "x" and Don't show again (also after a restart), Appearance scrolling back to the exact top, and Game options not re-flowing while scrolling.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/UiSmoke.ps1
```

It needs a visible desktop, Windows display scaling at 100%, and no mouse use while it runs (the window is placed at 100,100 at 1800x1100 and made topmost, and screenshots copy that screen area). Screenshots and the data folder are kept under `tests/out/ui-*`. For a one-off check of something new (a modal, a dropdown), write a small script with the helpers, look at the screenshots, and add an assertion to `UiSmoke.ps1` when it's worth keeping. Pages that need the network (Discover) work too, but the results change from day to day, so assert on layout rather than content.

`live_checks` is built with the tests but isn't a CTest test, because it calls the real pack APIs. Run it from the repository root after a change to a source client:

```powershell
build\Debug\live_checks.exe
```

It prints PASS or FAIL per source and skips CurseForge unless `MCLP_CURSEFORGE_API_KEY` is set. It also builds old Forge servers (1.7.10 from a Technic-style `bin/modpack.jar`, 1.6.4 from an ATLauncher-style jar mod) from Forge's maven, and starts each until it prints "Done". That takes a few minutes and downloads Java 8 the first time if none is installed. The servers are kept under `tests/out/live/` for inspection.

## Visual Studio

1. Configure the project once, then open `build\MC_Launcher_Premium.sln`.
2. Press **F5**. `MC_Launcher_Premium` is the startup project and runs from the repository root (`VS_STARTUP_PROJECT` and `VS_DEBUGGER_WORKING_DIRECTORY` in `CMakeLists.txt`).

Useful breakpoints:

| Area | Code |
| --- | --- |
| Search | `SearchPacks` in `src/Network/PackSources.cpp` |
| Whole-pack installs | `BuildInstallPlan` / `RunInstallPlan` in `src/Storage/InstallPlan.cpp` |
| Downloads | `HttpClient::DownloadToFile` in `src/Network/HttpClient.cpp` |
| Extraction | `InstallZip` in `src/Storage/Installer.cpp` |
| Play | `PlayModpack` in `src/Game/Launcher.cpp` |
| Sign-in | `src/Network/MicrosoftAuth.cpp` |
| UI | `AppShell::Draw` in `src/UI/Shell/AppShell.cpp`, called from `AppRoot::Frame` in `src/App/AppRoot.cpp` |

Searches, downloads, extraction, sign-in, and play run on worker threads. Use **Debug > Windows > Threads** to switch between them.

## VS Code

Install the **C/C++** and **CMake Tools** extensions, build, then add `.vscode/launch.json`:

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

Rebuild (`mcdebug build` or `cmake --build build --config Debug`) before each session, or add it as a `preLaunchTask`.

## Debug harness

`mcdebug debug` builds and runs the app in the current console and prints every API request, file change, child process and `app.log` line as it happens. `mcdebug debug http,file` limits it to some channels. See [features/debug-harness.md](features/debug-harness.md) for the channels, the output format and what is hidden.

## Logs

`app.log` in the repository root records startup, the data folder, packs loaded, each search and version lookup, installs, and errors. A normal start:

```
Starting app
Data folder: C:\path\to\MC_Launcher_Premium
DPI scale 1, UI scale 1, theme grass
Loaded 1 local modpack(s)
Modrinth search "": 30 of 18798 result(s)
Version lookup for fabulously-optimized: 14.1.1 (Fabulously.Optimized-v14.1.1.mrpack)
```

The log is buffered, so the last lines before a crash can be missing. Debug builds print failed ImGui assertions to stderr.

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| CurseForge page requires an API key, or reports that the key was rejected | See [CurseForge API key](../README.md#curseforge-api-key). |
| Empty pack list with a red status line | The source could not be reached. The error is shown in the status line and in `app.log`. Modrinth falls back to `resources\index.json`. |
| `Could not find resources\index.json` in `app.log` | The executable is outside the repository tree. Run it from inside the repository. |
| `Could not find a package configuration file provided by "glfw3"` (or another package) | `CMAKE_TOOLCHAIN_FILE` is missing or wrong. Re-run the configure command with the correct vcpkg path. |
| Errors in `imgui_impl_glfw.cpp` or `imgui_impl_opengl3.cpp` | The vcpkg ImGui version does not match the backend `GIT_TAG` in `CMakeLists.txt`. Make them match. |
| `LNK1104` or `LNK1168` for `MC_Launcher_Premium.exe` | The app is still running. Close it and rebuild. |

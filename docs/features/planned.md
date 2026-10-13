# Planned / Not Yet Implemented

Features that don't exist yet:

- **Microsoft sign-in approval:** the sign-in flow is built (see [accounts.md](accounts.md)), but it needs an Azure app client ID that Mojang has approved for the Minecraft API. Until one is set (Settings > Account, saved as `microsoftClientId`), only offline mode works.
- **Other platforms:** launching is Windows x64 only. See [Linux and Windows builds](#linux-and-windows-builds) below.
- **CurseForge in Browse:** Manage > Content > **Browse Modrinth** searches Modrinth only. CurseForge mods, resource packs and shaders (`/v1/mods/search` with class ids 6, 12 and 6552, files from `/v1/mods/{id}/files`, dependencies with `relationType` 3, installed mods found by CurseForge fingerprint) could be a second source when an API key is set. It needs a key to build and check against the real API.
- **Request cache on disk and in Manage:** the response cache ([request-caching.md](request-caching.md)) is in memory and starts empty on each launch, so the first Discover load of a session still asks every source (FTB, Technic and ATLauncher download their whole pack lists). Saving entries under the data folder with their age would carry them across launches. Manage > Content > Browse (`ModSearch`) is not on the cache yet and has no refresh button.
- **CurseForge download size:** Discover shows a version's download size for every source except CurseForge, which would have to download the pack zip and look up each file. Its API gives the pack zip's own size only.
- **Modpack source follow-ups:** a live CurseForge test with a real API key. The original spec is [plans/modpack-sources.md](../plans/modpack-sources.md).
- **Lower memory use:** the launcher sits at about 200 MB on launch and while browsing menus. See [Memory use](#memory-use) below.
- **Memory / network trade-off setting:** a Settings option to use less RAM or to cache more and call the network less. See [Memory use](#memory-use) below.

When one of these features is built, move it into the matching feature doc (or add a new one) and remove it here.

## Memory use

A plan only; none of it is implemented. A Debug build started offline measured 211 MB working set and 238 MB private bytes at launch, before any pack image was loaded. Release was not measured, and Debug uses more through the CRT debug heap. The font figure below comes from file sizes and the code, not from a profile, so check it first.

### Where the memory goes
1. **Fallback fonts (`LoadFonts()` in `src/UI/Theme.cpp`):** Segoe UI, then Microsoft YaHei (18.8 MB), Yu Gothic Medium (13.3 MB), Malgun Gothic (12.8 MB), Segoe UI Emoji (12.3 MB) and Segoe UI Symbol (2.4 MB) are merged into it, then the same five are merged into the bold font. `AddFontFromFileTTF` reads each whole file into heap memory, so about 60 MB of font files is held twice, roughly 120 MB. Rasterized glyphs are on demand, but the file bytes are not.
2. **Image textures (`ImageCache`):** every texture stays for the whole session and none is evicted, each with a full mipmap chain (about a third more). A 350 px banner thumbnail is about 0.65 MB and a 96 px icon about 0.05 MB. A full-size banner (`bannerHiResUrl`) or a library screenshot at full size can reach 2048² (about 22 MB each). A 100-result search plus a few opened packs can add 50 to 100 MB.
3. **Smaller items:**
   - `CachingHttpClient` holds up to 64 MB of response bodies, 8 MB per entry.
   - Each `ImageCache` worker holds the downloaded bytes and the decoded RGBA at once, so four workers on large WebP files briefly add tens of MB.
   - A server's console keeps 2000 lines (`Process.h`), and log reading is capped at 24 MB (`LogReader.cpp`).
   - The mascot render target is 256² (about 0.5 MB).

### Steps
1. **Measure:** temporarily disable `AddFallbackFonts()` and compare working set, then repeat on a Release build.
2. **Fonts:** memory-map each font file once and give the regular and bold fonts the same buffer (`ImFontConfig::FontData` with `FontDataOwnedByAtlas = false`), so only touched pages count and nothing is duplicated. Then load the CJK, symbol and emoji fallbacks only when text needs them, or skip those the system locale never uses.
3. **Textures:** evict least-recently-used textures once a size budget is passed (`Forget()` already deletes one), cap full-size banners and screenshots at about 1280 px instead of 2048, and skip mipmaps for images drawn near 1:1.
4. **Decode:** decode straight to the downscaled size so the full-size RGBA and the resized copy don't both exist.

### Memory / network setting
A Settings choice, say **Performance** with **Low memory** and **Fewer network calls**, that moves the limits above in opposite directions:

| Limit | Low memory | Fewer network calls |
|---|---|---|
| Texture budget in `ImageCache` | small (about 32 MB), evicts early | large (about 256 MB) or unlimited |
| Response cache total in `CachingHttpClient` (`maxTotalBytes`) | small (about 16 MB) | large (about 128 MB) |
| Response cache lifetime | short | long, and the request cache on disk (first list above) lets entries survive restarts |
| Fallback fonts | loaded only when a glyph needs them | loaded at launch |
| Full-size banner / screenshot side | 1024 px | 2048 px |

- Evicted images and expired responses are fetched again, so Low memory costs more downloads and Fewer network calls costs more RAM; a **Balanced** default sits between them.
- The limits are read from one struct in the services, not scattered through the UI, so the setting is one place that fills `ImageCache` and `CachingHttpClient::Limits` at startup and when it changes.
- It needs a `settings.json` field and a row in [settings.md](settings.md), and an update to [images.md](images.md) and [request-caching.md](request-caching.md) when built.

## Linux and Windows builds

This is a plan only; none of it is implemented. The goal is one source tree that builds a Windows x64 release and a Linux x64 release from CI, with the same features on both.

### Where things stand
- **Build:** CMake + vcpkg (`vcpkg.json`). Only Windows has been built: MSVC with the Visual Studio generator (README). `resources/app.rc` (icon) is added under `if (WIN32)`. All dependencies (glfw3, glad, imgui, nlohmann-json, curl, miniz, libwebp, stb) are portable and in vcpkg for `x64-linux`.
- **Already portable:**
  - **Game files:** `GameFiles.cpp` picks `kOsName`/`kJavaPlatform` per OS (`windows`/`windows-x64`, `osx`/`mac-os`, else `linux`), so the right natives and Mojang Java runtime are chosen.
  - **Launch arguments:** `Launcher.cpp` uses `;` or `:` for the classpath.
  - **Java search:** `JavaFinder.cpp` has a `java` (not `.exe`) branch and reads `JAVA_HOME`/`PATH` everywhere.
  - **Platform helpers:** `src/Platform/Platform_posix.cpp` opens folders and files with `xdg-open` and reads memory with `sysconf`. Its file picker, network adapter list and title bar are stubs (the picker returns nothing, the title bar is left alone). The app, services and pages reach the OS only through `src/Platform/Platform.h`.
- **Windows only today:** these `#else` branches are stubs that fail or do nothing.

| Area | File | Windows code | Linux replacement |
|---|---|---|---|
| Running Minecraft, installers and servers | `src/Util/Process.cpp` | `CreateProcessW` with a handle list, pipes, `CancelSynchronousIo`, `TerminateProcess` | `posix_spawn` (or `fork`/`execvp`) with `pipe2(O_CLOEXEC)` so children inherit only their own stdio; a reader thread on the pipe; `kill(SIGTERM)` then `SIGKILL`; `waitpid`. Without this nothing launches, so it comes first |
| Saved sign-in (`account.dat`) | `src/Storage/Account.cpp` | DPAPI (`CryptProtectData`) | libsecret (Secret Service) when available, else a file with mode 0600 and a clear note in accounts.md |
| Microsoft sign-in browser | `src/Network/MicrosoftAuth.cpp` | `ShellExecute` for the login page | `xdg-open` (shared helper in `Platform`) |
| Java search | `src/Game/JavaFinder.cpp` | registry (JavaSoft keys), `Program Files` vendor folders, MS Store runtime | `/usr/lib/jvm/*`, `/usr/lib64/jvm/*`, `/opt/*jdk*`, `~/.sdkman/candidates/java/*`, `~/.jdks`, Flatpak/Snap Prism and Mojang runtimes under `~/.local/share` and `~/.minecraft/runtime` |
| Long paths | `src/Util/Paths.cpp`, `src/Services/Library/UninstallService.cpp` | `\\?\` prefixes | none needed: `LongPath`/`ExtendedPath` return the path unchanged |
| Title bar color and icon | `src/Platform/Platform_posix.cpp` (`ApplyTitleBarStyle` is a no-op), `src/App/GlfwWindow.cpp` | DWM dark title bar, `app.rc` icon | `glfwSetWindowIcon` with `resources/logo.png` (works on both, replacing the `.rc` icon at runtime); the title bar follows the desktop theme |
| File picker | `src/Platform/Platform_win32.cpp` `PickFiles` | `IFileOpenDialog` | the XDG Desktop Portal over D-Bus, else `zenity --file-selection --multiple`, else return nothing (drag and drop and **Open folder** still work) |
| Data folder | `src/main.cpp` `FindDataRoot` | next to the exe | `$XDG_DATA_HOME/mc-launcher-premium` (default `~/.local/share/...`) when the exe's folder isn't writable, e.g. an AppImage |
| Dedicated server consoles | `Process.cpp` `ServerProcess` | as above | as above; Docker already works the same (`docker compose`) |
| Java executables | `Launcher.cpp` `CustomJava`, `GameFiles.cpp` runtimes | `javaw.exe` for the game, `java.exe` for servers and installers | `bin/java` for both (already the non-Windows fallback; downloaded runtimes are already marked executable) |

### Steps
1. **Process layer:** implement `RunProcess`, `ServerProcess` and `StartChild` for POSIX behind the existing interface, then run `launcher_checks` (its console and cancel checks use the test exe as a child) on Linux.
2. **Build on Linux:**
   - Add a `CMakePresets.json` with `windows-x64` (VS 2022, MSVC) and `linux-x64` (Ninja, GCC 13 or Clang 17) presets, both using the vcpkg toolchain and manifest.
   - Fix what GCC/Clang reject that MSVC accepts: `#pragma comment(lib, ...)` becomes `target_link_libraries`, and `_CRT_SECURE_NO_WARNINGS` only applies to MSVC.
   - On Linux, link `dl` and `pthread`, and use GLFW's X11 and Wayland backends.
3. **Per-area replacements** in the table above, one at a time, each with its doc updated (accounts.md, launching.md, servers.md, app-shell.md).
4. **CI** (GitHub Actions):
   - Run a matrix of `windows-latest` and `ubuntu-24.04`, with vcpkg binary caching.
   - Build Release, then run `launcher_checks` on both. `live_checks` runs nightly, since it needs the internet.
5. **Packaging:**
   - **Windows:** a zip of the exe plus `resources/`. Later, an Inno Setup or MSIX installer.
   - **Linux:** an AppImage (exe, `resources/`, desktop file and the 256 px logo), plus a plain `.tar.gz`. A Flatpak needs the portal file picker and the XDG data folder first.
   - Both have the version from `AppInfo::kVersion` in the file name.
6. **Check in the app on Linux:**
   - Sign in offline and with Microsoft.
   - Install one pack from each source, play it, and cancel a play.
   - Run a server and use its console, and try the Docker switch.
   - Add files with the picker and by drag and drop.
   - Check UI scaling at 100% and 200%.

### Out of scope
macOS (needs signing, notarization, an `.app` bundle and the `mac-os` runtimes), 32-bit and ARM builds.

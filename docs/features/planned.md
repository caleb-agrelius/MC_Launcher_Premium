# Planned / Not Yet Implemented

Features that don't exist yet:

- **Microsoft sign-in approval:** the sign-in flow is built (see [accounts.md](accounts.md)), but it needs an Azure app client ID that Mojang has approved for the Minecraft API. Until one is set (Settings > Account, saved as `microsoftClientId`), only offline mode works.
- **Servers for old Forge packs:** whole-pack servers need the loader declared in `pack.json`; Forge found in a pack's own jar (old ATLauncher and Technic packs) is only installed for the client.
- **Other platforms:** launching is Windows x64 only. See [Linux and Windows builds](#linux-and-windows-builds) below.
- **CurseForge in Browse:** Manage > Content > **Browse Modrinth** searches Modrinth only. CurseForge mods, resource packs and shaders (`/v1/mods/search` with class ids 6, 12 and 6552, files from `/v1/mods/{id}/files`, dependencies with `relationType` 3, installed mods found by CurseForge fingerprint) could be a second source when an API key is set. It needs a key to build and check against the real API.
- **Modpack source follow-ups:** a live CurseForge test with a real API key. The original spec is [plans/modpack-sources.md](../plans/modpack-sources.md).

When one of these features is built, move it into the matching feature doc (or add a new one) and remove it here.

## Linux and Windows builds

This is a plan only; none of it is implemented. The goal is one source tree that builds a Windows x64 release and a Linux x64 release from CI, with the same features on both.

### Where things stand
- **Build:** CMake + vcpkg (`vcpkg.json`). Only Windows has been built: MSVC with the Visual Studio generator (README). `resources/app.rc` (icon) is added under `if (WIN32)`. All dependencies (glfw3, glad, imgui, nlohmann-json, curl, miniz, libwebp, stb) are portable and in vcpkg for `x64-linux`.
- **Already portable:**
  - **Game files:** `GameFiles.cpp` picks `kOsName`/`kJavaPlatform` per OS (`windows`/`windows-x64`, `osx`/`mac-os`, else `linux`), so the right natives and Mojang Java runtime are chosen.
  - **Launch arguments:** `Launcher.cpp` uses `;` or `:` for the classpath.
  - **Java search:** `JavaFinder.cpp` has a `java` (not `.exe`) branch and reads `JAVA_HOME`/`PATH` everywhere.
  - **Platform helpers:** `Platform.cpp` opens folders and files with `xdg-open` and reads memory with `sysconf`.
- **Windows only today:** these `#else` branches are stubs that fail or do nothing.

| Area | File | Windows code | Linux replacement |
|---|---|---|---|
| Running Minecraft, installers and servers | `src/Util/Process.cpp` | `CreateProcessW` with a handle list, pipes, `CancelSynchronousIo`, `TerminateProcess` | `posix_spawn` (or `fork`/`execvp`) with `pipe2(O_CLOEXEC)` so children inherit only their own stdio; a reader thread on the pipe; `kill(SIGTERM)` then `SIGKILL`; `waitpid`. Without this nothing launches, so it comes first |
| Saved sign-in (`account.dat`) | `src/Storage/Account.cpp` | DPAPI (`CryptProtectData`) | libsecret (Secret Service) when available, else a file with mode 0600 and a clear note in accounts.md |
| Microsoft sign-in browser | `src/Network/MicrosoftAuth.cpp` | `ShellExecute` for the login page | `xdg-open` (shared helper in `Platform`) |
| Java search | `src/Game/JavaFinder.cpp` | registry (JavaSoft keys), `Program Files` vendor folders, MS Store runtime | `/usr/lib/jvm/*`, `/usr/lib64/jvm/*`, `/opt/*jdk*`, `~/.sdkman/candidates/java/*`, `~/.jdks`, Flatpak/Snap Prism and Mojang runtimes under `~/.local/share` and `~/.minecraft/runtime` |
| Long paths | `src/Util/Paths.cpp`, `main.cpp` uninstall | `\\?\` prefixes | none needed: `LongPath`/`ExtendedPath` return the path unchanged |
| Title bar color and icon | `main.cpp`, `src/UI/Theme.cpp` | DWM dark title bar, `app.rc` icon | `glfwSetWindowIcon` with `resources/logo.png` (works on both, replacing the `.rc` icon at runtime); the title bar follows the desktop theme |
| File picker | `src/Platform.cpp` `PickFiles` | `IFileOpenDialog` | the XDG Desktop Portal over D-Bus, else `zenity --file-selection --multiple`, else return nothing (drag and drop and **Open folder** still work) |
| Data folder | `main.cpp` `FindDataRoot` | next to the exe | `$XDG_DATA_HOME/mc-launcher-premium` (default `~/.local/share/...`) when the exe's folder isn't writable, e.g. an AppImage |
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
   - Both have the version from `kAppVersion` in the file name.
6. **Check in the app on Linux:**
   - Sign in offline and with Microsoft.
   - Install one pack from each source, play it, and cancel a play.
   - Run a server and use its console, and try the Docker switch.
   - Add files with the picker and by drag and drop.
   - Check UI scaling at 100% and 200%.

### Out of scope
macOS (needs signing, notarization, an `.app` bundle and the `mac-os` runtimes), 32-bit and ARM builds.

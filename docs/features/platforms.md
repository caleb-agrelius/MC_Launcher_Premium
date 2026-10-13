# Windows and Linux

The launcher builds for Windows x64 and Linux x64 from one source tree. Windows is the platform it has been used on; the Linux build compiles and passes `launcher_checks` in CI, but nobody has used the app on a Linux desktop yet (see [planned.md](planned.md#linux)). macOS, 32-bit and ARM are not supported.

Code outside `src/Platform/` doesn't include OS headers in new code (see [architecture.md](../architecture.md)); a new OS call is declared in `Platform/Platform.h` and defined in both `Platform_win32.cpp` and `Platform_posix.cpp`. CMake builds the one that matches. `launcher_checks` runs on both.

## Where the two differ

| Area | Windows | Linux |
|---|---|---|
| Running Minecraft, installers and servers (`src/Util/Process.cpp`) | `CreateProcessW` with a handle list, so a child inherits only its own standard handles; no console window | `posix_spawn` with the standard streams set up and every other descriptor close-on-exec (`pipe2(O_CLOEXEC)`, `src/Util/PosixFd.h`), so a child inherits only its own stdio |
| Ending a program | `TerminateProcess` | `SIGTERM`, then `SIGKILL` if it is still there after 3 s (2 s for a server's **Kill**); a program left running is reaped by a helper thread |
| A server's console reader | `ReadFile`, stopped with `CancelSynchronousIo` when a program the server started still holds the pipe | `poll` with a 100 ms timeout and a stop flag, for the same case; `SIGPIPE` is ignored so writing to a stopped server fails instead of ending the launcher |
| Exit code | the program's | the program's, or 128 plus the signal that ended it |
| Saved sign-in (`account.dat`) | encrypted with DPAPI (`Platform::ProtectSecret`) | plain JSON, but the file is limited to its owner (mode 0600, `Platform::RestrictToOwner`) before anything is written |
| Microsoft sign-in browser and redirect | `ShellExecuteA`; Winsock listener on `127.0.0.1` | `xdg-open`; BSD-socket listener on `127.0.0.1` (`Platform::OpenUrl`, `Platform::LoopbackListener`) |
| Open folder / file | Explorer (`ShellExecuteW`) | `xdg-open`, started without a shell |
| **Add files...** picker | `IFileOpenDialog` | `zenity --file-selection --multiple`, else `kdialog --getopenfilename`, else nothing (drag and drop still works) |
| Network adapters (Servers > Host IP) | `GetAdaptersAddresses` | `getifaddrs`: interfaces that are up, not loopback, not 169.254.x.x |
| Java executable (`JavaExecutableName`) | `javaw.exe` for the game (no console), `java.exe` for servers and installers | `bin/java` for both |
| Java search (`FindJavaInstalls`) | `JAVA_HOME`, `PATH`, the registry, `Program Files` vendor folders, `~/.jdks`, the Minecraft Store and Prism runtimes | `JAVA_HOME`, `PATH` (each `java` resolved through its links to its real home), `/usr/lib/jvm`, `/usr/lib64/jvm`, `/usr/java`, `/opt` and `/opt/java`, `~/.jdks`, `~/.sdkman/candidates/java`, `~/.minecraft/runtime`, Prism's `java` folder (also as a Flatpak or Snap) |
| Game platform (`kOsName`, `kJavaPlatform`) | `windows`, `windows-x64` | `linux`, `linux` |
| Classpath separator | `;` | `:` |
| Forge and NeoForge server arguments (`ForgeArgsFileName`) | `win_args.txt` | `unix_args.txt` |
| Long paths (`Util/Paths`) | `\\?\` prefixes past 240 characters | unchanged |
| Window icon | the logo, set at runtime (`glfwSetWindowIcon`) and in `resources/app.rc` | the logo at runtime; the title bar follows the desktop (`ApplyTitleBarStyle` does nothing) |
| UI fonts (`Theme::LoadFonts`) | Segoe UI, with Microsoft YaHei, Yu Gothic, Malgun Gothic, Segoe UI Symbol and Emoji merged in | Noto Sans, else DejaVu Sans, with Noto CJK merged in when installed; else ImGui's built-in font |
| Data folder | the folder holding `resources/` | the same, or `$XDG_DATA_HOME/mc-launcher-premium` (else `~/.local/share/mc-launcher-premium`) when that folder can't be written to |
| Display sync | `DwmFlush` | the driver's vsync (`WaitForDisplayRefresh` is false) |

## Data folder

`main()` finds the folder holding `resources/index.json` and runs from it (see [app-shell.md](app-shell.md)). On Linux, when that folder can't be written to (an AppImage, an install under `/usr`), `WritableDataRoot` uses `Platform::UserDataDir()` instead and links `resources/` into it (`resources -> <install>/resources`, made again on every start, because an AppImage mounts itself somewhere new each time). The settings, library, downloads, installs and `app.log` then live in the user folder, and every relative path in the app keeps working. Windows has no such folder, so a Windows install must be writable.

## Building

`CMakePresets.json` has two configure presets, both using the vcpkg toolchain from `$VCPKG_ROOT`:

| Preset | For |
|---|---|
| `windows-x64` | the newest installed Visual Studio, x64, in `build/` |
| `linux-x64` | Ninja, Release, in `build/linux-x64/` |

```sh
export VCPKG_ROOT=~/vcpkg
cmake --preset linux-x64
cmake --build --preset linux-x64
ctest --preset linux-x64
```

The Linux build needs these system packages for GLFW, besides a C++17 compiler, CMake, Ninja and git: `pkg-config`, `autoconf`, `automake`, `libtool`, `xorg-dev`, `libxmu-dev`, `libxi-dev`, `libgl-dev`, `libxinerama-dev`, `libxcursor-dev`, `libglu1-mesa-dev`, `libwayland-dev`, `libxkbcommon-dev`, `wayland-protocols` and `extra-cmake-modules` (Debian and Ubuntu names). At run time it needs an OpenGL 3 driver, and optionally `zenity` or `kdialog` for the file picker, `xdg-utils` for **Open folder** and the sign-in browser, and Docker for the Servers page's Docker switch.

Every target gets `GLFW_INCLUDE_NONE` (glad supplies the OpenGL declarations), links `Threads::Threads` and the system's `dl`, and on MSVC `_CRT_SECURE_NO_WARNINGS`.

## CI

`.github/workflows/ci.yml` runs on every pull request and calls two reusable workflows: `build.yml` (Windows, Visual Studio) and `build-linux.yml` (ubuntu-24.04, GCC and Ninja). The Linux one clones vcpkg (left on master; the manifest's `builtin-baseline` is read from its history, so the port versions don't change), restores the built vcpkg packages from the Actions cache and saves them even when a later step fails, installs the ImGui backends once, builds `MC_Launcher_Premium` and `launcher_checks` with `-k 0` (every failing file is reported, not just the first), and runs `launcher_checks` through CTest with `MCLP_OFFLINE=1`. Both are Debug builds.

`launcher_checks` covers the process layer on both systems: a console program driven through `ServerProcess` (input, output, quoting, a second start refused, a child that inherits the pipe), a program cancelled while running, and a program traced without its tokens. It also covers SHA-256 and the PKCE challenge (RFC 7636's example), the sign-in redirect parser, the loopback listener (libcurl as the browser), and `account.dat` (DPAPI on Windows, mode 0600 on Linux).

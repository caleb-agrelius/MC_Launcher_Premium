# MC_Launcher_Premium

MC_Launcher_Premium is a Minecraft Java Edition modpack browser and downloader built with Dear ImGui and C++.

Features:
- Browse a sample modpack index (resources/index.json)
- Download files via libcurl with progress
- Extract ZIPs to an installs/ folder using miniz

Building and running on Windows:
- Requires CMake (>=3.14), a C++17 compiler, and vcpkg.
- Set the `VCPKG_ROOT` environment variable to your vcpkg installation directory.
- Open this repository as a folder in VS Code and open a new integrated terminal. The workspace settings add the repository directory to that terminal's `PATH`.
- Run `mcdebug build` to configure and build the Debug executable.
- Run `mcdebug run` to build and launch the application.
- Run `mcdebug help` to see the available commands.

The command uses the vcpkg manifest in `vcpkg.json` to install project dependencies when CMake configures the build. Run the launcher from its repository directory so it can find `resources/index.json`. Downloads are saved to `downloads/` and extracted to `installs/<modpack-id>/` when you click Install.

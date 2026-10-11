# MC_Launcher_Premium

MC_Launcher_Premium is a Minecraft Java Edition modpack browser and downloader built with Dear ImGui and C++.

Features:
- Browse a sample modpack index (resources/index.json)
- Download files via libcurl with progress
- Extract ZIPs to an installs/ folder using miniz

Building:
- Requires CMake (>=3.14), a C++17 compiler, libcurl, and OpenGL dev libs
- Configure and build with CMake:

  mkdir build
  cd build
  cmake ..
  cmake --build .

Run the produced executable (MC_Launcher_Premium). The UI shows the sample index. Downloads are saved to downloads/ and extracted to installs/<modpack-id>/ when you click Install.

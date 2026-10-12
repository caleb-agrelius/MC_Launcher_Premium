# Project layout

## Source

| Path | Contents |
| --- | --- |
| `src/main.cpp` | Window, render loop, pages, background workers |
| `src/Platform.*` | Opening folders, system memory |
| `src/Model/Modpack.*` | `Modpack` / `PackFile` structs, `resources/index.json` loader |
| `src/Network/HttpClient.*` | libcurl wrapper: downloads, GET/POST, headers, cancellation |
| `src/Network/PackSources.*` | Single entry point for search, versions, and installs across sources |
| `src/Network/Modrinth.*` | Modrinth API client |
| `src/Network/CurseForge.*` | CurseForge API client and API key handling |
| `src/Network/Ftb.*` | FTB API client |
| `src/Network/Technic.*` | Technic Platform and Solder client |
| `src/Network/AtLauncher.*` | ATLauncher CDN client |
| `src/Network/MicrosoftAuth.*` | Microsoft, Xbox Live, and Minecraft sign-in |
| `src/Storage/Installer.*` | Zip extraction |
| `src/Storage/InstallPlan.*` | Whole-pack installs: download, verify, merge overrides |
| `src/Storage/Settings.*` | `settings.json` |
| `src/Storage/Library.*` | `library.json` and `downloads.json` |
| `src/Storage/Account.*` | `account.dat` |
| `src/Game/GameFiles.*` | Minecraft versions, libraries, assets, Java runtimes |
| `src/Game/Loaders.*` | Fabric, Quilt, Forge, and NeoForge installation |
| `src/Game/PackInstance.*` | `.mrpack` index to `instances/<pack>/` |
| `src/Game/GameOptions.*` | `options.txt` defaults applied to every pack |
| `src/Game/Launcher.*` | Play: preparation, command line, process launch |
| `src/Game/ServerSetup.*` | Dedicated server setup, properties, EULA and launch commands |
| `src/UI/Theme.*` | Color themes, fonts, DPI and UI scaling |
| `src/UI/Widgets.*` | Shared widgets and animations |
| `src/UI/SettingsPage.*` | Settings page |
| `src/UI/Toasts.*` | Toast notifications |
| `src/UI/ImageCache.*` | Asynchronous icon and banner loading (PNG, JPEG, GIF, WebP) |
| `src/UI/MobModel.*`, `src/UI/Mobs.*` | Sidebar mob rendering and randomly selected box-built mobs |
| `src/Util/Hash.*`, `src/Util/Sha1.*` | SHA-1 and MD5 |
| `src/Util/Paths.*` | Long (260+ character) Windows paths |
| `src/Util/Process.*` | Process launching and interactive server console |
| `src/Util/Text.*` | Markdown/HTML descriptions to plain text, shortening |
| `src/assets/` | `minecraft_fox.glb` |
| `resources/index.json` | Offline fallback pack list |
| `docs/features/` | Feature documentation, indexed in `CLAUDE.md` |
| `tests/` | Regression checks, opt-in live API checks (`LiveChecks.cpp`), and Windows window smoke test |
| `mcdebug.cmd` | Build and run helper (see the [README](../README.md#mcdebug)) |

## Data folder

The app runs from the repository root (the folder containing `resources\index.json`) and creates the following there. All are gitignored.

| Path | Contents |
| --- | --- |
| `app.log` | Startup information, searches, version lookups, installs, errors |
| `imgui.ini` | Window layout |
| `settings.json` | Settings (see [features/settings.md](features/settings.md)) |
| `library.json` | Packs listed in My Library |
| `downloads.json` | Finished downloads, restored on startup |
| `account.dat` | Signed-in account, encrypted with Windows DPAPI |
| `downloads\<pack>\` | Downloaded `.mrpack` files and pack archives |
| `installs\<pack>\` | Extracted pack files; whole-pack installs use `minecraft\` and `pack.json` |
| `minecraft\` | Shared Minecraft versions, libraries, assets, and Java runtimes |
| `instances\<pack>\` | Game directory for each played pack (mods, config, saves, logs) |
| `servers\<pack>\` | Dedicated server files, world, configuration and console logs |

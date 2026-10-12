# Project layout

## Source

| Path | Contents |
| --- | --- |
| `src/main.cpp` | Window, render loop, pages, background workers |
| `src/Platform.*` | Opening folders, the system Open dialog (`PickFiles`), system memory, network addresses |
| `src/Model/Modpack.*` | `Modpack` / `PackFile` structs, `resources/index.json` loader |
| `src/Network/HttpClient.*` | libcurl wrapper: downloads, GET/POST, headers, cancellation |
| `src/Network/PackSources.*` | Single entry point for search, versions, and installs across sources |
| `src/Network/Modrinth.*` | Modrinth API client |
| `src/Network/CurseForge.*` | CurseForge API client and API key handling |
| `src/Network/ModSearch.*` | Modrinth mods, resource packs and shader packs for Manage > Content > Browse: search, fitting versions, installing with dependencies |
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
| `src/Game/PackInstance.*` | `.mrpack` index to the version's game folder (`instances/<pack>@<version>/`) |
| `src/Game/JavaFinder.*` | Finding installed Javas (registry, common folders, Minecraft/Prism runtimes) and picking one per Minecraft version |
| `src/Game/InstanceContent.*` | Mods, resource packs and shader packs in a game or server folder: listing, on/off, removing, jar metadata |
| `src/Game/GameOptions.*` | `options.txt` defaults applied to every pack |
| `src/Game/PackCreator.*` | New packs: Minecraft and loader version lists, pack ids, the new pack's `pack.json` |
| `src/Game/Launcher.*` | Play: preparation, command line, process launch |
| `src/Game/ServerSetup.*` | Dedicated server setup, properties, EULA and launch commands |
| `src/UI/Theme.*` | Color themes, fonts, DPI and UI scaling |
| `src/UI/Widgets.*` | Shared widgets and animations |
| `src/UI/Logo.*` | The logo's pixels and wordmark font |
| `src/UI/LogView.*` | Selectable, copyable log and console text |
| `src/UI/BackgroundTasks.h` | How pages run slow work on a worker and post results back to the UI thread |
| `src/UI/ManagePage.*` | A pack's Manage page |
| `src/UI/NewPackDialog.*` | My Library > New pack... |
| `src/UI/SettingsPage.*` | Settings page |
| `src/UI/Toasts.*` | Toast notifications |
| `src/UI/ImageCache.*` | Asynchronous icon and banner loading (PNG, JPEG, GIF, WebP) |
| `src/UI/MobModel.*`, `src/UI/Mobs.*` | Sidebar mob rendering and randomly selected box-built mobs |
| `src/Util/Hash.*`, `src/Util/Sha1.*` | SHA-1 and MD5 |
| `src/Util/Paths.*` | Long (260+ character) Windows paths |
| `src/Util/Process.*` | Process launching and interactive server console |
| `src/Util/Text.*` | Markdown/HTML descriptions to plain text, shortening, compact counts (`FormatCount`), file sizes (`FormatFileSize`), `ToLower` |
| `src/Util/Json.h` | `StringOrEmpty`: a JSON field as a string, or empty when it's missing or not a string |
| `src/assets/` | `minecraft_fox.glb` |
| `resources/index.json` | Offline fallback pack list |
| `resources/logo.ico`, `resources/logo.png`, `resources/app.rc` | The logo as the executable's icon, written by `logo_export` |
| `tools/LogoExport.cpp` | `logo_export`: writes `resources/logo.ico` and `resources/logo.png` |
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

# Settings

The Settings page and `settings.json`. UI code is `src/UI/SettingsPage.{h,cpp}`. Storage is `src/Storage/Settings.{h,cpp}`.

## Page
Open it from **Settings** under LAUNCHER in the sidebar. `DrawSettingsPage(settings, context)` edits `Settings` in place and returns true when something changed. `main()` then marks the settings dirty, saves them once no widget is being edited, and applies theme and scale changes at the start of the next frame.

Each tab is a set of cards with a label (and hint) on the left and the control on the right. Pill buttons along the top switch tabs.

| Tab | Contents |
|---|---|
| **Appearance** | Theme picker (a grid of `ThemeCard`s, applied on click); UI scale slider (75–200%) with **Reset**; **Reduce motion** and **Gallery banners** toggles |
| **Minecraft** | Defaults used for every pack when Play is pressed (see [launching.md](launching.md)). **Memory:** maximum (`-Xmx`, 1 GB up to the PC's RAM, warning above 75% of it) and starting (`-Xms`, Automatic or a fixed amount) sliders in 0.5 GB steps. **Java:** executable path (empty = Mojang's runtime; a red note appears if the path doesn't exist) and extra JVM arguments. **Game window:** fullscreen, custom window size (off = Minecraft's 854 x 480), and what the launcher does when the game starts. **Reset Minecraft settings to defaults** |
| **Account** | The current account (name and type, or "Not signed in") with a pointer to the sidebar's ACCOUNT section, and the **Azure client ID** used for Microsoft sign-in (`microsoftClientId`, see [accounts.md](accounts.md)) |
| **Downloads** | Results per search (10–100; CurseForge shows at most 50); the **CurseForge API key** (masked, with **Show**/**Hide**; empty uses the `MCLP_CURSEFORGE_API_KEY` environment variable, see [curseforge.md](curseforge.md)); the `downloads/`, `installs/`, `instances/` and `minecraft/` paths with **Open** buttons |
| **About** | Version, Windows display scaling, system memory, the data folder (with **Open**), keyboard shortcuts, and a pack-sources / not-affiliated note |

`SettingsPageContext` passes in the app version (`kAppVersion` in `main.cpp`), the data folder, total RAM (`Platform::SystemMemoryMb`), the DPI scale, the current account's name and type, and whether `MCLP_CURSEFORGE_API_KEY` is set. **Open** buttons use `Platform::OpenFolder`, which creates the folder if needed and opens it in Explorer.

## `settings.json`
Saved in the data folder (gitignored). A missing or invalid file gives the defaults. Values are clamped on load.

| Key | Default | Notes |
|---|---|---|
| `uiScale` | 1.0 | 0.75–2.0, saved rounded to 2 decimals |
| `theme` | `"grass"` | A theme id from [ui-theme.md](ui-theme.md). Unknown ids fall back to Grass Block |
| `reduceMotion` | false | No image fade-ins or toast animations |
| `showGalleryBanners` | true | When false, pack banners don't load gallery images |
| `searchLimit` | 30 | Results per search, 10–100 (CurseForge caps it at 50) |
| `discoverSource` | `"modrinth"` | The last Discover sub-page, a source key (see [discover.md](discover.md)). Unknown keys fall back to the first source |
| `microsoftClientId` | `""` | Azure app ID for Microsoft sign-in; empty disables it |
| `curseForgeApiKey` | `""` | CurseForge API key, stored in plain text; empty uses `MCLP_CURSEFORGE_API_KEY` |
| `launch.minMemoryMb` | 0 | `-Xms`; 0 = let Java decide; at most `maxMemoryMb` |
| `launch.maxMemoryMb` | 4096 | `-Xmx`; 1024–32768. A top-level `maxMemoryMb` (older files) is read into this |
| `launch.javaPath` | `""` | Empty = Mojang's runtime for the Minecraft version |
| `launch.jvmArgs` | `""` | Extra JVM arguments |
| `launch.windowWidth` / `windowHeight` | 0 / 0 | 0 = Minecraft's default size; otherwise at least 320 |
| `launch.fullscreen` | false | |
| `launch.afterLaunch` | 0 | 0 keep open, 1 minimize, 2 close (`AfterLaunch`) |

How the `launch` values are applied is described in [launching.md](launching.md).

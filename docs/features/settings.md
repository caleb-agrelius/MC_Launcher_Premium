# Settings

The Settings page and `settings.json`. UI code is `src/UI/SettingsPage.{h,cpp}`. Storage is `src/Storage/Settings.{h,cpp}`.

## Page
Open it from **Settings** under LAUNCHER in the sidebar. `DrawSettingsPage(settings, context)` edits `Settings` in place and returns true when something changed. `main()` then marks the settings dirty, saves them once no widget is being edited, and applies theme and scale changes at the start of the next frame.

Each tab is a set of cards with a label (and hint) on the left and the control on the right. Pill buttons along the top switch tabs.

| Tab | Contents |
|---|---|
| **Appearance** | Theme picker (a grid of `ThemeCard`s, applied on click, plus **Create your own**); the custom theme editor when a custom theme is selected (see [ui-theme.md](ui-theme.md)); UI scale slider (75–200%) with **Reset** (the only place to change it besides the keyboard shortcuts); **Reduce motion** and **Gallery banners** toggles |
| **Minecraft** | Defaults used for every pack when Play is pressed (see [launching.md](launching.md)). **Memory:** maximum (`-Xmx`, 1 GB up to the PC's RAM, warning above 75% of it) and starting (`-Xms`, Automatic or a fixed amount) sliders in 0.5 GB steps. **Java:** executable path (empty = Mojang's runtime; a red note appears if the path doesn't exist) and extra JVM arguments. **Game window:** fullscreen, custom window size (off = Minecraft's 854 x 480), and what the launcher does when the game starts. **Reset Minecraft settings to defaults** |
| **Game options** | Vanilla Minecraft options applied to every pack (see below): how they're applied (every launch, or only a pack's first launch), how many are on, **Turn all off**, then cards for Video, Controls, Sound, Chat & accessibility, and First launch |
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
| `reduceMotion` | false | No page transitions, bubbling lists, image fade-ins, toast animations or smooth scrolling |
| `showGalleryBanners` | true | When false, pack banners don't load gallery images |
| `searchLimit` | 30 | Results per search, 10–100 (CurseForge caps it at 50) |
| `discoverSources` | `[]` | Discover source filters (source keys); empty selects all sources. Unknown keys are ignored |
| `window` | omitted | Last normal launcher size and position (`width`, `height`, `x`, `y`, in screen pixels) and `maximized`; saved at shutdown |
| `microsoftClientId` | `""` | Azure app ID for Microsoft sign-in; empty disables it |
| `curseForgeApiKey` | `""` | CurseForge API key, stored in plain text; empty uses `MCLP_CURSEFORGE_API_KEY` |
| `launch.minMemoryMb` | 0 | `-Xms`; 0 = let Java decide; at most `maxMemoryMb` |
| `launch.maxMemoryMb` | 4096 | `-Xmx`; 1024–32768. A top-level `maxMemoryMb` (older files) is read into this |
| `launch.javaPath` | `""` | Empty = Mojang's runtime for the Minecraft version |
| `launch.jvmArgs` | `""` | Extra JVM arguments |
| `launch.windowWidth` / `windowHeight` | 0 / 0 | 0 = Minecraft's default size; otherwise at least 320 |
| `launch.fullscreen` | false | |
| `launch.afterLaunch` | 0 | 0 keep open, 1 minimize, 2 close (`AfterLaunch`) |
| `packLaunch` | `{}` | Per-pack launch options: pack id -> the same keys as `launch` except `afterLaunch`. Missing keys fall back to `launch`. Set from My Library > Manage > Launch settings |
| `gameOptions.everyLaunch` | true | false = apply only the first time each pack starts |
| `gameOptions.values` | `{}` | options.txt key -> value, only for options turned on |
| `customThemes` | `[]` | Custom themes, see [ui-theme.md](ui-theme.md) |

How the `launch` values are applied is described in [launching.md](launching.md). `LaunchSettingsFor(settings, packId)` picks a pack's own options from `packLaunch` when it has them, always with the global `afterLaunch`. The Minecraft page's notice mentions per-pack settings when any pack has them.

## Game options (`src/Game/GameOptions.{h,cpp}`)
- `GameOptionDefs()` lists the vanilla options the page offers, with their options.txt key, group, label, hint, type (`Toggle`, `Slider`, `Choice`, `Text`), vanilla default, and for sliders the range and unit conversion. Keys and value formats follow recent Minecraft (1.19+); versions that don't know a key ignore it.
- Each row has an on/off switch. Off (the default) means the option isn't written, so the pack's or the player's own setting stays. Turning it on stores the vanilla default; the control next to it then edits the value.
- Sliders are shown in friendly units and converted when saved (`GameOptionToDisplay` / `GameOptionFromDisplay`): field of view in degrees (Minecraft stores (degrees - 70) / 40), mouse sensitivity in % (stored / 200), volumes and other 0-1 values in %. Max framerate 260 shows as "Unlimited".
- Inverted toggles (e.g. **Skip the accessibility intro**, stored as `onboardAccessibility:false`) show "on" when the stored value is `false`.
- `ApplyGameOptions()` runs at Play after the pack's overrides are copied (see [launching.md](launching.md)). It rewrites `options.txt` in the version's game folder, replacing the lines for options that are on and appending missing ones, and keeps every other line. The file is written to a temporary file and renamed into place. With **Only the first time each pack starts**, `.mclp-options-applied` in the instance marks it as done.

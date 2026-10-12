# Settings

The Settings page and `settings.json`. UI code is `src/UI/SettingsPage.{h,cpp}`. Storage is `src/Storage/Settings.{h,cpp}`.

## Page
Open it from **Settings** under LAUNCHER in the sidebar. `DrawSettingsPage(settings, context)` edits `Settings` in place and returns true when something changed. `main()` then marks the settings dirty, saves them once no widget is being edited, and applies theme and scale changes at the start of the next frame.

Each tab is a set of cards with a label (and hint) on the left and the control on the right. Pill buttons along the top switch tabs.

| Tab | Contents |
|---|---|
| **Appearance** | Theme picker (a grid of `ThemeCard`s, applied on click); UI scale slider (75–200%) with **Reset**; **Reduce motion** and **Gallery banners** toggles |
| **Minecraft** | A notice that launching isn't available yet. **Memory:** maximum (`-Xmx`) and minimum (`-Xms`) sliders in 0.5 GB steps, up to the PC's RAM, with a warning above 75% of it. **Java:** executable path (empty = automatic) and extra JVM arguments. **Game window:** resolution, fullscreen, and what the launcher does when the game starts. **Reset Minecraft settings to defaults** |
| **Account** | A placeholder: a notice, a block-art avatar, "Not signed in", and a disabled **Sign in with Microsoft** button |
| **Downloads** | Results per search (10–100); the `downloads/` and `installs/` paths with **Open** buttons |
| **About** | Version, Windows display scaling, system memory, the data folder (with **Open**), keyboard shortcuts, and a Modrinth / not-affiliated note |

`SettingsPageContext` passes in the app version (`kAppVersion` in `main.cpp`), the data folder, total RAM (`Platform::SystemMemoryMb`), and the DPI scale. **Open** buttons use `Platform::OpenFolder`, which creates the folder if needed and opens it in Explorer.

## `settings.json`
Saved in the data folder (gitignored). A missing or invalid file gives the defaults. Values are clamped on load.

| Key | Default | Notes |
|---|---|---|
| `uiScale` | 1.0 | 0.75–2.0, saved rounded to 2 decimals |
| `theme` | `"grass"` | A theme id from [ui-theme.md](ui-theme.md). Unknown ids fall back to Grass Block |
| `reduceMotion` | false | No image fade-ins or toast animations |
| `showGalleryBanners` | true | When false, pack banners don't load gallery images |
| `searchLimit` | 30 | Modrinth results per search, 10–100 |
| `launch.minMemoryMb` | 2048 | At least 512, at most `maxMemoryMb` |
| `launch.maxMemoryMb` | 4096 | At least 512 |
| `launch.javaPath` | `""` | Empty = automatic |
| `launch.jvmArgs` | `""` | |
| `launch.windowWidth` / `windowHeight` | 1280 / 720 | At least 320 |
| `launch.fullscreen` | false | |
| `launch.afterLaunch` | 0 | 0 keep open, 1 minimize, 2 close (`AfterLaunch`) |

The `launch` values aren't used yet. See [planned.md](planned.md).

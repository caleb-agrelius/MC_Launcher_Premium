# UI Theme and Scaling

Color themes, fonts, DPI and UI scaling, shared drawing helpers, and toast notifications. The settings file is described in [settings.md](settings.md).

## Scaling (`src/UI/Theme.{h,cpp}`)
- The total scale is the **monitor DPI scale** times the **user UI scale**.
  - The DPI scale comes from `glfwGetWindowContentScale`, e.g. 1.5 at 150% Windows display scaling (common on 4K monitors).
  - The UI scale is a user setting from 75% to 200% in 5% steps, saved in `settings.json`.
- `Theme::Apply(themeIndex, dpiScale, userScale)` rebuilds the whole ImGui style:
  - Sets `style.FontScaleDpi` and `style.FontScaleMain`. Text is rendered at the scaled size, not stretched, because ImGui 1.92 rasterizes fonts at the size they are drawn.
  - Scales padding, spacing, rounding and so on with `ScaleAllSizes`.
- The frame loop calls `Apply` again when the window moves to a monitor with a different DPI, or when the UI scale or theme changes.
- `Theme::S(px)` scales a design-time pixel size. Every hardcoded layout size goes through it, e.g. `S(230.0f)` for the sidebar width.
- Changing the UI scale:
  - The UI scale slider on Settings > Appearance (applied when the slider is released, so the UI doesn't rescale under the mouse while dragging), and its **Reset** button.
  - **Ctrl +** / **Ctrl -** / **Ctrl 0** (number row or keypad).
  - Changes apply on the next frame and are saved immediately.

## Fonts
- `Theme::LoadFonts()` loads Segoe UI (`segoeui.ttf`) as the regular font and Segoe UI Semibold (`seguisb.ttf`, falling back to `segoeuib.ttf`) as the bold font.
- Both fonts merge in these fallbacks, so pack names and descriptions in other scripts don't render as `?`:
  - Microsoft YaHei (Chinese)
  - Yu Gothic (Japanese)
  - Malgun Gothic (Korean)
  - Segoe UI Symbol
  - Segoe UI Emoji (in one color)
- Glyphs are rasterized on demand, so the fallback fonts cost nothing until they're used.
- On other systems, or if Segoe UI is missing, ImGui's scalable default font (`AddFontDefaultVector`) is used.
- Unscaled sizes for `ImGui::PushFont(font, size)`:

  | Constant | Size |
  |---|---|
  | `kFontSmall` | 13.5 |
  | `kFontBody` | 16 (the default) |
  | `kFontHeading` | 19 |
  | `kFontTitle` | 30 |
  | `kFontHero` | 34 |

## Themes
- `Theme::Themes()` lists the 10 built-in themes, then the player's custom themes (`ThemeInfo`: `id`, `name`, `description`, `Palette`, `custom`). The id is what `settings.json` stores. An unknown id (for example a deleted custom theme) falls back to the first theme.

  | id | Name | Look |
  |---|---|---|
  | `grass` | Grass Block | The original slate and green (default) |
  | `deep_dark` | Deep Dark | Near-black with sculk cyan |
  | `nether` | Nether | Netherrack red and lava orange |
  | `the_end` | The End | Void purple with end stone yellow |
  | `prismarine` | Prismarine | Ocean teal |
  | `cherry` | Cherry Grove | Blossom pink |
  | `sandstone` | Sandstone | Desert gold |
  | `redstone` | Redstone | Stone grey with red |
  | `diamond` | Diamond | Midnight blue with icy cyan |
  | `birch` | Birch | Light theme |

- `Theme::C()` returns the active `Palette`. Code reads colors from it (e.g. `Theme::C().accent`), never hardcoded values, so everything follows the theme.

  | Field | Use |
  |---|---|
  | `windowBg` / `panelBg` / `cardBg` | Window / panels / cards |
  | `frameBg` | Text boxes, sliders, unselected tab pills |
  | `border` | Borders and separators |
  | `text` / `textDim` | Normal / secondary text |
  | `accent` | Selection bars, progress, accent text, badges, toggles |
  | `accentDeep` | Buttons and selected rows; also the fallback pack color |
  | `error` | Error text and error toasts |
  | `info` | Loader chips, info notices and toasts |
  | `light` | True for light themes: ImGui's light base style, darker chip text, dark outlines, light title bar |

- `Apply` derives the remaining ImGui colors (hover and active shades, slider grabs, scrollbars) from these by mixing (`Theme::Mix`).
- To add a built-in theme, add an entry to `kThemes` in `Theme.cpp`. It appears on Settings > Appearance automatically.

## Custom themes
- Made on Settings > Appearance: the dashed **Create your own** card (`Widgets::NewThemeCard`) at the end of the theme grid copies the selected theme's colors into a new theme (`custom-<n>`, "My theme <n>") and selects it.
- While a custom theme is selected, an editor card appears below the grid: name, **Start from** (copy a built-in theme's colors), **Light theme**, and a color picker with a hex field for each palette field (`Theme::PaletteFields()`, in `Palette` order). **Duplicate** copies the theme; **Delete theme** removes it and switches back to Grass Block.
- Edits apply live. Custom themes are saved in `settings.json` under `customThemes` (`id`, `name`, `light`, and `colors`: 11 RGB triples in `PaletteFields()` order). `Theme::SetCustomThemes()` turns them into `ThemeInfo`s and bumps `Theme::Revision()` when anything changed, which tells the frame loop to re-apply the style.
- `Theme::C()` returns a copy of the palette made by the last `Apply()`, so editing or deleting a custom theme never leaves it pointing at a removed entry.

## Settings (`src/Storage/Settings.{h,cpp}`)
- `settings.json` in the data folder is gitignored. Its fields, including `uiScale`, `discoverSources` and the saved launcher window placement, are listed in [settings.md](settings.md).
- `LoadSettings` returns the defaults if the file is missing or invalid, and clamps the scale to its range.
- `SaveSettings` writes the scale rounded to two decimals.

## Widgets (`src/UI/Widgets.{h,cpp}`)

| Widget | What it draws |
|---|---|
| `BeginCard` / `EndCard` | A full-width bordered card with a measured, remembered height; offscreen cards keep their height, and the mouse wheel scrolls the surrounding page |
| `Text(font, size, color, text)` | Text in a given font, size, and color |
| `SectionLabel` | Small bold dimmed heading, e.g. "PACK FILES (1)" |
| `Spinner` | A rotating arc the height of a text line |
| `Chip` | A rounded pill tag tinted with a color |
| `ThinProgressBar` | An 8px accent progress bar with no text |
| `Toggle` | An on/off switch in the theme's colors |
| `NavItem` | A sidebar entry with an accent bar when selected, and an optional count badge |
| `NavGroupItem` | A `NavItem` heading a collapsible group, with a chevron (down when expanded, right when collapsed) |
| `SubNavItem` | A smaller, indented sidebar entry with a dot (accent-colored when selected), used for Discover's source sub-pages |
| `ListItem` | A text row for lists and combo popups (Discover's loader and sort filters); closes its popup when clicked |
| `WrappedText` | Colored text that wraps at the window edge (Discover's error statuses) |
| `EmptyState` | A centered block illustration, title, hint, and optional button for empty pages |
| `ThemeCard` | A theme swatch: a miniature launcher drawn in the theme's colors, its name, and a check mark when selected |
| `PackRow` | A pack list row (see [discover.md](discover.md)) |
| `HeroBanner` | The banner at the top of the pack details pane (see [discover.md](discover.md)) |
| `DrawPackIcon` | The pack icon, or a placeholder tile in the pack's color with its first letter, shown until the icon loads or if it fails |

Images fade in over 0.25 s (`FadeAlpha`), unless **Reduce motion** is on (`Widgets::SetReduceMotion`).

Library and Downloads share card padding, 56px icons and heading-size pack names. Long text wraps, and action buttons move below it when there is insufficient room on the same row. Settings uses the same card measurement with its own padding.

The sidebar mascot uses `MobModel` and `Mobs`: a bundled fox model or box-built pig, creeper, cow, chicken, sheep, slime or bee, chosen randomly at startup. Clicking makes it hop; right-clicking chooses another. Reduced motion disables hopping and idle bobbing.

## Motion
All of this is off when **Reduce motion** is on.
- **Entrance** (`Widgets::BeginEntrance(start, index)` / `EndEntrance()`): the item fades in and rises 14 px (scaled) into place over 0.32 s, ease-out cubic. It multiplies the current style alpha and lifts the cursor, then takes the lift back out so items below don't move. `index` delays each list item by 45 ms (at most 10 steps), so lists "bubble" in one after another. Used for:
  - the main content when the page or Discover source changes (`pageShownAt` in `main.cpp`)
  - Discover's result rows, when the page opens or new results arrive (`SourceView::resultsAt`)
  - Discover's details pane, when another pack is selected
  - My Library and Downloads cards
  - Settings cards, when the page opens or the tab changes (`SettingsPageContext::shownAt`)
- **Smooth scrolling** (`Widgets::SmoothScroll()`, called before `EndChild()` of a child created with `ImGuiWindowFlags_NoScrollWithMouse`): each wheel notch moves a target 120 px (scaled) and the view eases toward it (about 70 ms time constant). The target is kept in the window's `ImGuiStorage`. Children end before their parents, so the innermost scrollable window under the mouse takes the wheel. Scrollbar drags, keyboard navigation and `SetScrollHereY` reset the target. Ctrl+wheel is ignored. Used by the main content, Discover's pack list and details pane, and the Settings page. With reduce motion on, the wheel jumps straight to the target.

**Selection highlights:** `NavItem`, `SubNavItem`, `PackRow` and `ListItem` share `RoundedSelectable`, an `InvisibleButton` with its own highlight instead of `ImGui::Selectable`. The highlight is a rounded rectangle (`ImGuiCol_Header` when selected, `HeaderHovered` on hover, `HeaderActive` while pressed). It is drawn inside the content region, so it never reaches the panel's padding or border. The accent selection bar sits just inside the highlight's left edge.

## Toasts (`src/UI/Toasts.{h,cpp}`)
- `Toasts::Push(kind, title, body)` queues a notification. Kinds are `Info`, `Success`, and `Error`, and set the color of the stripe on the left. Call it on the UI thread; background threads post through `UiQueue`.
- `Toasts::Draw(reduceMotion)` draws them in the bottom-right corner, newest at the bottom, each in its own non-interactive window. A toast stays 4.5 s, fading and sliding in and out unless reduce motion is on. At most 4 are kept.
- Current toasts: download complete or failed, files extracted or extraction failed, whole-pack install finished or failed, Minecraft started, and play failed.

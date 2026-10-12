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

- `Apply` derives every other ImGui color from these by mixing (`Theme::Mix`) and transparency (`Theme::WithAlpha`). That covers hover and active shades, slider grabs and scrollbars, plus dialog title bars (`TitleBg*`), the dimmed page behind dialogs (`ModalWindowDimBg`), tabs (`Tab*`, e.g. Discover's Description and Mods tabs), links (`TextLink`), separators, resize grips, tables, tree lines and drag targets. So no dialog or tab keeps ImGui's default blue. `launcher_checks` (`ThemeColorChecks`) fails if any built-in theme leaves a visible color slot at ImGui's stock value.
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

## Logo (`src/UI/Logo.{h,cpp}`)
- The logo is a pixel-art grass block in isometric view with a dark outline. `Logo::BlockPixels(size)` renders it as RGBA from a 32x32 original: smaller sizes are averaged down, larger ones are scaled up in whole pixels, so it stays crisp.
- `Logo::Glyph` is a 5x7 blocky pixel font for the wordmark.
- Where it appears:
  - The window's title bar and taskbar icon (`glfwSetWindowIcon`, 16 to 64 px).
  - The executable's icon: `resources/logo.ico`, linked through `resources/app.rc`. `logo_export` (`tools/LogoExport.cpp`, built with the app) rewrites `resources/logo.ico` and `resources/logo.png`. Run it from the repository root after changing the logo.
  - In the app: `Widgets::LogoBlock(size)` draws the block, and `Widgets::Wordmark(text, pixel, face, depth)` draws pixel-font text with an extruded edge, like the Minecraft title. The sidebar shows the "MC LAUNCHER" wordmark, and Settings > About shows the block and wordmark.

## Widgets (`src/UI/Widgets.{h,cpp}`)

| Widget | What it draws |
|---|---|
| `BeginCard` / `EndCard` | A full-width bordered card with a measured, remembered height; offscreen cards keep their height, and the mouse wheel scrolls the surrounding page |
| `Text(font, size, color, text)` | Text in a given font, size, and color |
| `SectionLabel` | Small bold dimmed heading, e.g. "PACK FILES (1)" |
| `Spinner` | A rotating arc the height of a text line |
| `ModifiedChip` | The amber "Modified" chip for a pack version the player changed, with the changes ("1 added, 1 turned off") in its tooltip. Used everywhere an installed version shows (see [manage.md](manage.md#your-changes)). `ModifiedColor()` is its color, for related text |
| `Chip` | A rounded pill tag tinted with a color. It centers on the line it shares: in the middle of a button-height line (after `AlignTextToFramePadding` or next to buttons), or on a line of plain text without pushing the next line down |
| `ThinProgressBar` | An 8px accent progress bar with no text |
| `Toggle` | An on/off switch in the theme's colors |
| `ToggleRow` | A `Toggle` with its label after it (clicking the label toggles too), used instead of checkboxes |
| `PrimaryButton` / `DangerButton` | The main action button (accent fill, e.g. PLAY, Start server) and the destructive one (red tint, e.g. Stop game, Uninstall, Delete) |
| `ButtonWidth` | The width `ImGui::Button` gives a label, for right-aligning button rows |
| `BeginSelect` / `EndSelect` | A dropdown drawn as one rounded frame with the choice (shortened with "..." when long) and a chevron that points up while open, instead of ImGui's square arrow box. Filled with `ListItem` rows |
| `Select` | A whole `BeginSelect` dropdown over fixed labels (game mode, difficulty, Settings choices) |
| `MenuItem` | A popup menu entry built on `ListItem`, with an optional shortcut hint and a disabled state (log view context menu, Add account) |
| `PaneSplitter` | A draggable gap between two side-by-side panes; shows a grip and a resize cursor on hover and changes the left pane's width |
| `NavItem` | A sidebar entry with an accent bar when selected, and an optional count badge |
| `NavGroupItem` | A `NavItem` heading a collapsible group, with a chevron (down when expanded, right when collapsed) |
| `SubNavItem` | A smaller, indented sidebar entry with a dot (accent-colored when selected), used for Discover's source sub-pages |
| `ListItem` | A text row for lists and dropdowns; closes its popup when clicked (unless asked not to, like the version list's Retry). An optional hint is drawn dimmed on the right (a version's Minecraft version, an account's type), and in a popup the row asks for room for both so the popup widens to fit |
| `WrappedText` | Colored text that wraps at the window edge (Discover's error statuses) |
| `EmptyState` | A centered block illustration, title, hint, and optional button for empty pages |
| `ThemeCard` | A theme swatch: a miniature launcher drawn in the theme's colors, its name, and a check mark when selected |
| `PackRow` | A pack list row (see [discover.md](discover.md)) |
| `HeroBanner` | The banner at the top of the pack details pane (see [discover.md](discover.md)) |
| `DrawPackIcon` | The pack icon, or a placeholder tile in the pack's color with its first letter, shown until the icon loads or if it fails |

Images fade in over 0.25 s (`FadeAlpha`), unless **Reduce motion** is on (`Widgets::SetReduceMotion`).

Every button, dropdown, list row, switch and menu entry comes from these widgets, so they share the theme's rounding and colors. No page uses `ImGui::Combo`, `ImGui::Selectable`, `ImGui::Checkbox` or `ImGui::MenuItem` directly, or pushes its own button colors.

**Resizable panes:** Discover's pack list, the Servers list and the Manage page's log list have a `PaneSplitter` on their right edge. Discover and Servers keep their widths in `settings.json` (`paneWidths`, see [settings.md](settings.md)), and the Manage log list keeps its width while the launcher runs. Each width is kept between a minimum and what leaves the pane beside it usable.

**Scrollbars:** the grab floats inside its track (`ScrollbarPadding`, 3 px scaled), and `BeginScrollArea` ends content 6 px (scaled) before a visible vertical scrollbar, so rows, cards and highlights never touch it.

Library and Downloads share card padding, 56px icons and heading-size pack names. Long text wraps, and action buttons move below it when there is insufficient room on the same row. Settings uses the same card measurement with its own padding.

The sidebar mascot uses `MobModel` and `Mobs`: a bundled fox model or box-built pig, creeper, cow, chicken, sheep, slime or bee, chosen randomly at startup. Clicking makes it hop; right-clicking chooses another. Reduced motion disables hopping and idle bobbing.

## Log view (`src/UI/LogView.{h,cpp}`)
- `LogView::Draw(id, lines, size, firstLine)` shows read-only text lines in a bordered, smooth-scrolling area with a horizontal scrollbar. Only visible lines are drawn, so long logs stay fast.
- Click and drag selects text (by character, across lines); dragging past the top or bottom edge scrolls. A double-click selects a line. Ctrl+C copies the selection, Ctrl+A selects everything, and the right-click menu has **Copy**, **Select all** and **Copy everything**. The scrollbars don't start a selection.
- Lines with `ERROR`, `FATAL` or `Exception` are tinted with the error color; `WARN` lines with a softer shade.
- While the view is scrolled to the bottom it follows new lines. `firstLine` is the number of `lines[0]`, so a console that drops old lines keeps a selection on the same text. `ScrollToBottom()` and `ClearSelection()` reset the view, e.g. when another file is shown.
- Used by the Manage page's Logs tab.

## Motion
All of this is off when **Reduce motion** is on.
- **Entrance** (`Widgets::BeginEntrance(start, index)` / `EndEntrance()`): the item fades in and rises 14 px (scaled) into place over 0.32 s, ease-out cubic. It multiplies the current style alpha and lifts the cursor, then takes the lift back out so items below don't move. `index` delays each list item by 45 ms (at most 10 steps), so lists "bubble" in one after another. Used for:
  - the main content when the page or Discover source changes (`pageShownAt` in `main.cpp`)
  - Discover's result rows, when the page opens or new results arrive (`SourceView::resultsAt`)
  - Discover's details pane, when another pack is selected
  - My Library and Downloads cards
  - Settings cards, when the page opens or the tab changes (`SettingsPageContext::shownAt`)
- **Smooth scrolling** (`Widgets::SmoothScroll()`, called before `EndChild()` of a child created with `ImGuiWindowFlags_NoScrollWithMouse`): each wheel notch moves a target 120 px (scaled) and the view eases toward it (about 70 ms time constant). The target is kept in the window's `ImGuiStorage`. Scrollbar drags, keyboard navigation and `SetScrollHereY` reset the target. Ctrl+wheel is ignored. With reduce motion on, the wheel jumps straight to the target.
  - **Which window scrolls** (`WheelTarget`, worked out once per frame): starting from the window under the mouse, the first window that has something to scroll and takes the wheel, walking up through parent child windows. Cards (no wheel of their own) hand it to the page around them, so the wheel works the same over text, cards, sliders and empty space. The walk stops at a popup or top-level window, so the wheel over an open combo list (such as a version picker) scrolls only that list, never the page behind it.
  - `Widgets::BeginScrollArea(id, size, childFlags, windowFlags)` / `EndScrollArea()` make a child window that scrolls this way. Every scrolling panel uses it or `SmoothScroll` directly: the main content, Discover's pack list and details pane, the Servers list, details, mod list and console, Settings, and the launch-settings and optional-mods dialogs.
- **Tabs** (`Widgets::TabStrip(id, labels, count, &current)`): a row of rounded pill buttons, the selected one filled with `accentDeep` in the bold font. Used by Settings and Discover's details pane.

**Selection highlights:** `NavItem`, `SubNavItem`, `PackRow` and `ListItem` share `RoundedSelectable`, an `InvisibleButton` with its own highlight instead of `ImGui::Selectable`. The highlight is a rounded rectangle (`ImGuiCol_Header` when selected, `HeaderHovered` on hover, `HeaderActive` while pressed). It is drawn inside the content region, so it never reaches the panel's padding or border. The accent selection bar sits just inside the highlight's left edge.

## Toasts (`src/UI/Toasts.{h,cpp}`)
- `Toasts::Push(kind, title, body)` queues a notification. Kinds are `Info`, `Success`, and `Error`, and set the color of the stripe on the left. Call it on the UI thread; background threads post through `UiQueue`.
- `Toasts::Draw(reduceMotion)` draws them in the bottom-right corner, newest at the bottom, each in its own non-interactive window. A toast stays 4.5 s, fading and sliding in and out unless reduce motion is on. At most 4 are kept.
- Current toasts: download complete or failed, files extracted or extraction failed, whole-pack install finished or failed, Minecraft started, and play failed.

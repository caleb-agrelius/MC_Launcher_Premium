# UI Theme and Scaling

Colors, fonts, DPI and UI scaling, the settings file, and shared drawing helpers.

## Scaling (`src/UI/Theme.{h,cpp}`)
- The total scale is the **monitor DPI scale** times the **user UI scale**.
  - The DPI scale comes from `glfwGetWindowContentScale`, e.g. 1.5 at 150% Windows display scaling (common on 4K monitors).
  - The UI scale is a user setting from 75% to 200% in 5% steps, saved in `settings.json`.
- `Theme::Apply(dpiScale, userScale)` rebuilds the whole ImGui style:
  - Sets `style.FontScaleDpi` and `style.FontScaleMain`. Text is rendered at the scaled size, not stretched, because ImGui 1.92 rasterizes fonts at the size they are drawn.
  - Scales padding, spacing, rounding and so on with `ScaleAllSizes`.
- The frame loop calls `Apply` again when the window moves to a monitor with a different DPI, or when the UI scale changes.
- `Theme::S(px)` scales a design-time pixel size. Every hardcoded layout size goes through it, e.g. `S(230.0f)` for the sidebar width.
- Changing the UI scale:
  - The sidebar footer's `-` and `+` buttons.
  - Clicking the percentage resets it to 100%.
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

## Palette
Dark slate backgrounds with green accents:

| Constant | Use |
|---|---|
| `kAccent` (0.40, 0.83, 0.50) | Accent text, selection bars, progress bars, chips |
| `kAccentDeep` | Buttons; also the fallback pack color |
| `kError` (1.0, 0.42, 0.38) | Error text |
| `kText` / `kTextDim` | Normal / secondary text |
| `kWindowBg` / `kPanelBg` / `kCardBg` | Window / panels / cards, from darkest to lightest |
| `kBorder` | Borders and separators |

On Windows 10/11 the native title bar is switched to dark mode.

## Settings (`src/Storage/Settings.{h,cpp}`)
- `settings.json` in the data folder is gitignored. Today it only holds `{"uiScale": 1.0}`.
- `LoadSettings` returns the defaults if the file is missing or invalid, and clamps the scale to its range.
- `SaveSettings` writes the scale rounded to two decimals.

## Widgets (`src/UI/Widgets.{h,cpp}`)

| Widget | What it draws |
|---|---|
| `Text(font, size, color, text)` | Text in a given font, size, and color |
| `SectionLabel` | Small bold dimmed heading, e.g. "PACK FILES (1)" |
| `Spinner` | A rotating arc the height of a text line |
| `Chip` | A rounded pill tag tinted with a color |
| `ThinProgressBar` | An 8px accent progress bar with no text |
| `NavItem` | A sidebar entry with a green bar when selected |
| `PackRow` | A pack list row (see [discover.md](discover.md)) |
| `HeroBanner` | The banner at the top of the pack details pane (see [discover.md](discover.md)) |
| `DrawPackIcon` | The pack icon, or a placeholder tile in the pack's color with its first letter, shown until the icon loads or if it fails |

Images fade in over 0.25 s (`FadeAlpha`).

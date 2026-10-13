# Planned / Not Yet Implemented

Features that don't exist yet:

- **Microsoft sign-in approval:** the sign-in flow is built (see [accounts.md](accounts.md)), but it needs an Azure app client ID that Mojang has approved for the Minecraft API. Until one is set (Settings > Account, saved as `microsoftClientId`), only offline mode works.
- **Linux:** the Linux build compiles and passes `launcher_checks` in CI ([platforms.md](platforms.md)), but the app hasn't been used on a Linux desktop. See [Linux](#linux) below.
- **Request cache on disk and in Manage:** the response cache ([request-caching.md](request-caching.md)) is in memory and starts empty on each launch, so the first Discover load of a session still asks every source (FTB, Technic and ATLauncher download their whole pack lists). Saving entries under the data folder with their age would carry them across launches. Manage > Content > Browse (`ModSearch`) is not on the cache yet and has no refresh button.
- **CurseForge download size:** Discover shows a version's download size for every source except CurseForge, which would have to download the pack zip and look up each file. Its API gives the pack zip's own size only.
- **Modpack source follow-ups:** a live CurseForge test with a real API key, for Discover and for Manage > Content > Browse ([curseforge.md](curseforge.md#verification)). `live_checks` runs both when `MCLP_CURSEFORGE_API_KEY` is set. The original spec is [plans/modpack-sources.md](../plans/modpack-sources.md).
- **Lower memory use:** the launcher sits at about 200 MB on launch and while browsing menus. See [Memory use](#memory-use) below.
- **Memory / network trade-off setting:** a Settings option to use less RAM or to cache more and call the network less. See [Memory use](#memory-use) below.
- **Skip PR builds for non-code changes:** `.github/workflows/ci.yml` runs the full Debug build and offline checks on every pull request, including docs-only ones. Add a `paths-ignore` filter to its `pull_request` trigger for files that can't affect the build (`**.md`, `docs/**`, `LICENSE`, `.gitignore`, `.gitattributes`), and keep `CMakeLists.txt`, `vcpkg.json`, `src/**`, `tests/**`, `resources/**` (the app reads `resources/index.json` at startup) and `.github/workflows/**` running it. If CI is a required status check, a workflow skipped by `paths-ignore` never reports and docs-only PRs can't merge; in that case make the check not required, or use an always-running job that detects changed paths and have the build job depend on it. `release.yml` still builds and checks Release before publishing, so no release coverage is lost.

When one of these features is built, move it into the matching feature doc (or add a new one) and remove it here.

## Memory use

A plan only; none of it is implemented. A Debug build started offline measured 211 MB working set and 238 MB private bytes at launch, before any pack image was loaded. Release was not measured, and Debug uses more through the CRT debug heap. The font figure below comes from file sizes and the code, not from a profile, so check it first.

### Where the memory goes
1. **Fallback fonts (`LoadFonts()` in `src/UI/Theme.cpp`):** Segoe UI, then Microsoft YaHei (18.8 MB), Yu Gothic Medium (13.3 MB), Malgun Gothic (12.8 MB), Segoe UI Emoji (12.3 MB) and Segoe UI Symbol (2.4 MB) are merged into it, then the same five are merged into the bold font. `AddFontFromFileTTF` reads each whole file into heap memory, so about 60 MB of font files is held twice, roughly 120 MB. Rasterized glyphs are on demand, but the file bytes are not.
2. **Image textures (`ImageCache`):** every texture stays for the whole session and none is evicted, each with a full mipmap chain (about a third more). A 350 px banner thumbnail is about 0.65 MB and a 96 px icon about 0.05 MB. A full-size banner (`bannerHiResUrl`) or a library screenshot at full size can reach 2048² (about 22 MB each). A 100-result search plus a few opened packs can add 50 to 100 MB.
3. **Smaller items:**
   - `CachingHttpClient` holds up to 64 MB of response bodies, 8 MB per entry.
   - Each `ImageCache` worker holds the downloaded bytes and the decoded RGBA at once, so four workers on large WebP files briefly add tens of MB.
   - A server's console keeps 2000 lines (`Process.h`), and log reading is capped at 24 MB (`LogReader.cpp`).
   - The mascot render target is 256² (about 0.5 MB).

### Steps
1. **Measure:** temporarily disable `AddFallbackFonts()` and compare working set, then repeat on a Release build.
2. **Fonts:** memory-map each font file once and give the regular and bold fonts the same buffer (`ImFontConfig::FontData` with `FontDataOwnedByAtlas = false`), so only touched pages count and nothing is duplicated. Then load the CJK, symbol and emoji fallbacks only when text needs them, or skip those the system locale never uses.
3. **Textures:** evict least-recently-used textures once a size budget is passed (`Forget()` already deletes one), cap full-size banners and screenshots at about 1280 px instead of 2048, and skip mipmaps for images drawn near 1:1.
4. **Decode:** decode straight to the downscaled size so the full-size RGBA and the resized copy don't both exist.

### Memory / network setting
A Settings choice, say **Performance** with **Low memory** and **Fewer network calls**, that moves the limits above in opposite directions:

| Limit | Low memory | Fewer network calls |
|---|---|---|
| Texture budget in `ImageCache` | small (about 32 MB), evicts early | large (about 256 MB) or unlimited |
| Response cache total in `CachingHttpClient` (`maxTotalBytes`) | small (about 16 MB) | large (about 128 MB) |
| Response cache lifetime | short | long, and the request cache on disk (first list above) lets entries survive restarts |
| Fallback fonts | loaded only when a glyph needs them | loaded at launch |
| Full-size banner / screenshot side | 1024 px | 2048 px |

- Evicted images and expired responses are fetched again, so Low memory costs more downloads and Fewer network calls costs more RAM; a **Balanced** default sits between them.
- The limits are read from one struct in the services, not scattered through the UI, so the setting is one place that fills `ImageCache` and `CachingHttpClient::Limits` at startup and when it changes.
- It needs a `settings.json` field and a row in [settings.md](settings.md), and an update to [images.md](images.md) and [request-caching.md](request-caching.md) when built.

## Linux

The Linux build, its CI and the differences from Windows are described in [platforms.md](platforms.md). What is left:

1. **Use the app on a Linux desktop.** Everything below the window has been checked only by `launcher_checks` in CI. Try, on X11 and on Wayland:
   - sign in offline and with Microsoft (needs the approved client ID above)
   - install one pack from each source, play it, and cancel a play
   - run a server, use its console, and try the Docker switch
   - add files with the picker and by drag and drop
   - UI scaling at 100% and 200%, and the fonts on a distribution without Noto or DejaVu
2. **Saved sign-in in the Secret Service.** `account.dat` is plain JSON limited to its owner on Linux. libsecret would keep the tokens in the desktop's keyring when one is running, with the file as the fallback.
3. **File picker through the XDG Desktop Portal** (D-Bus). Today it runs `zenity` or `kdialog`, so there is no picker when neither is installed or inside a Flatpak sandbox.
4. **Packaging.** `release.yml` publishes only the Windows zip.
   - Linux: an AppImage (the executable, `resources/`, a desktop file and the 256 px logo) and a `.tar.gz`. An AppImage is read-only, which the per-user data folder already allows for.
   - A Flatpak needs the portal picker first.
   - Both would carry the version from `AppInfo::kVersion` in the file name.
5. **A nightly `live_checks` run** on both systems (it needs the internet, so it isn't part of the pull request build).

Out of scope: macOS (needs signing, notarization, an `.app` bundle and the `mac-os` runtimes), 32-bit and ARM builds.

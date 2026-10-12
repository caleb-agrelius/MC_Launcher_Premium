# Plan: More Modpack Sources (CurseForge, FTB, Technic, ATLauncher)

An implementation spec for adding modpack sources besides Modrinth. It is based on how Prism Launcher does it (`launcher/modplatform/` on Prism's `develop` branch). This describes **future work**. When a source ships, document it under `docs/features/` and update `CLAUDE.md`, `planned.md` and this file (see "Docs to update").

Read first: [modrinth-search.md](../features/modrinth-search.md), [downloads.md](../features/downloads.md), [installation.md](../features/installation.md), [discover.md](../features/discover.md), [images.md](../features/images.md).

---

## 1. API keys and credentials

| Source | Key needed? | What to do |
|---|---|---|
| **Modrinth** | No | Already implemented. Keep sending `User-Agent`. |
| **CurseForge** | **Yes**, required on every request | Apply for our own key (see below). **Do not copy Prism's key.** Prism's `CMakeLists.txt` says the key "was issued specifically for Prism Launcher" and "CurseForge requires you to change this if you make any kind of derivative work." |
| **FTB** | No | Public API. Download one file at a time (see section 4). |
| **Technic** | No key. Requires a `build` query parameter. | Prism sends `build=multimc`, an identifier issued to MultiMC. **Open question:** ask Technic which `build` value a new launcher should send, and use that. Until then, the implementer should confirm that the API still accepts requests without a value tied to another launcher. Optional `cid` (client ID) parameter, also unconfirmed. |
| **ATLauncher** | No | Public JSON on their CDN. |

### CurseForge key setup
1. Apply for a key through the CurseForge for Studios console (`https://console.curseforge.com/`). The application asks what the project is. Their 3rd-party API terms are linked from Prism's CMake comment: `https://support.curseforge.com/en/support/solutions/articles/9000207405-curse-forge-3rd-party-api-terms-and-conditions`.
2. The key looks like a bcrypt hash (`$2a$10$...`). Note the `$` characters when passing it through a shell or CMake.
3. **Never commit the key.** Supply it in this order:
   - At runtime, the environment variable `MCLP_CURSEFORGE_API_KEY` overrides everything.
   - At build time, the CMake cache variable `CURSEFORGE_API_KEY` (default empty) is passed as a compile definition, e.g. `target_compile_definitions(... PRIVATE MCLP_CURSEFORGE_API_KEY="${CURSEFORGE_API_KEY}")`. The `.vcxproj` build needs the same define, empty by default. Values live in `CMakeCache.txt` or `CMakeUserPresets.json`, which are already gitignored.
4. If no key is configured, show the CurseForge source in the UI as disabled, with the message "CurseForge API key not configured". Don't crash or send keyless requests.
5. The key ends up inside the shipped binary. Every desktop launcher has this; CurseForge's terms allow it.
6. Add the setup steps to `README.md` when CurseForge ships.

---

## 2. Shared infrastructure (done)

Shipped and documented in [pack-sources.md](../features/pack-sources.md):
- HTTP headers, POST and per-thread connection reuse
- SHA-1/MD5 (`src/Util/Hash`)
- `PackSource` on `Modpack`
- the install plan runner
- `ExtractZip` with a prefix, and `IsSafeRelativePath`
- one Discover sub-page per source, with per-source search filters (Minecraft version, loader, sort)

The root `.vcxproj` was not updated: it was already stale (no `Library`, `Settings` or `UI/` files) and the README says it doesn't build. CMake is the build.

Each new source must:
- add itself to `OnlineSources()`, `GetSourceCapabilities()`, `SearchPacks()`, `FetchPackVersion()` and `BuildInstallPlan()` in `src/Network/PackSources.cpp`
- set `installsPacks`

---

## 3. CurseForge (done, except a live test with a real key)

Shipped and documented in [curseforge.md](../features/curseforge.md); key setup is in the README. **Still to do:** once a CurseForge API key is available, run a live search, a version lookup, and a full install, and record the pack used in `curseforge.md`.

---

## 4. FTB (done)

Shipped and documented in [ftb.md](../features/ftb.md). Verified with a full install of FTB Magic World 2 (911 files).

---

## 5. Technic (done)

Shipped and documented in [technic.md](../features/technic.md). The `build` question is settled: Technic accepts our own non-numeric identifier (`mclauncherpremium`). Verified with Tekkit (Solder) and Minecraft Breakdown (single zip) installs. Loader detection for single-zip packs is still open.

---

## 6. ATLauncher (done)

Shipped and documented in [atlauncher.md](../features/atlauncher.md). Verified with a full install of Lithium (Fabric) and a plan of The Last Days (blocked `browser` file, optional mods). Optional mods are skipped until there's a picker.

---

## 7. Implementation order

1. Shared infrastructure (section 2): HTTP headers and POST, hashing, `PackSource`, the plan runner, path-safety reuse, and the source selector UI.
2. **CurseForge**: the most-used source. It is also needed for FTB files hosted on CurseForge. Blocked if there's no key; FTB and Technic don't depend on step 2 apart from FTB's CurseForge-hosted files.
3. **FTB**: small manifest; reuses the CurseForge resolver.
4. **Technic**: zip downloads and extraction. The `build` value is an open question (section 1).
5. **ATLauncher**: the most complex manifest.

Test each source end to end with one small real pack, and write down which pack was used in the feature doc.

## 8. Docs to update when each source ships
- New `docs/features/<source>.md` (e.g. `curseforge.md`) describing the client, endpoints, mapping and limitations, plus a row in the `CLAUDE.md` table.
- `discover.md` (source selector), `downloads.md` (multi-file plans, the extra headers), `installation.md` (`minecraft/` layout, `pack.json`, override prefix, blocked files), `modpack-index.md` (`PackSource`), `images.md` (icon sources other than Modrinth), `modrinth-search.md` (remove "CurseForge" from "Not yet supported" if it's listed; note `version_files` if it's added there).
- Remove the shipped parts from this plan and `planned.md`. Delete this file when everything here is done.
- `README.md`: CurseForge key setup (section 1).

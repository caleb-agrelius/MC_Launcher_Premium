# Technic

The Technic Discover sub-page: search, version lookup, and whole-pack installs through the Technic Platform API (`https://api.technicpack.net/`). Code: `src/Network/Technic.{h,cpp}`, called through [pack-sources.md](pack-sources.md).

## The `build` parameter
Technic answers `401 Unauthorized` unless every request carries a `build` value.
- Prism Launcher sends `multimc`, an identifier issued to MultiMC.
- We send our own, `mclauncherpremium` (`kBuild`). The API accepts non-numeric identifiers like this one; numeric values such as old launcher build numbers can be rejected.
- No API key is needed.

## Search (`SearchTechnicModpacks`)
- With no query, it calls `GET trending?build=...`. With a query, it calls `GET search?build=...&q=<text>`. Both return `modpacks[] { name, slug, url, iconUrl }`.
- The `vanilla` slug is skipped. The API returns every match at once, and the list is cut to 30.
- **Mapping:**
  - `id` = `"technic-" + slug`
  - `sourceId` = the slug
  - `name`, `iconUrl` and `websiteUrl` (`url`) come from the result.
- **Not in search results:** author, downloads and Minecraft version. Until a pack is selected, its row shows "Technic".
- **Filters and sorts:** the API has neither. When the **Minecraft version** filter or the **Most installed** sort is chosen:
  - each result's details are fetched (up to 60 packs, 4 at a time)
  - packs whose Minecraft version doesn't match are dropped (`1.12` matches `1.12.x`)
  - the rest are sorted by installs

  Those rows then show their version and downloads.
- **Not offered:** a loader filter, because Technic doesn't say which loader a pack uses.
- The default sort, **Trending**, keeps the API's order.

## Version (`FetchTechnicVersion`)
- **Details:** `GET modpack/{slug}?build=...` fills in:
  - the author (`user`), downloads (`installs`) and description
  - the Minecraft version (`minecraft`)
  - the page (`platformUrl`) and the banner (`background.url`, else `logo.url`)
- **Single-zip packs:** if `url` is a string, the pack is a single zip. `sourceUrl` is that zip, and the version is the pack's `version`. A string `url` wins even when `solder` is also set, as in Prism.
- **Solder packs:** otherwise `solder` is the pack's Solder API base, with any trailing `/` removed.
  - `GET {solder}/modpack/{slug}` gives the builds.
  - The **recommended** build is installed, else the **latest**.
  - `sourceIsSolder` is set and the build becomes `sourceVersionId`.

## Install (`BuildTechnicPlan`)
- **Solder:** `GET {solder}/modpack/{slug}/{build}` returns `minecraft` and `mods[] { name, version, url, md5, filesize }`.
  - Every mod is a zip laid out like the game folder (`mods/`, `config/`, `bin/`...).
  - Each zip is downloaded, checked against its MD5, and unpacked into `minecraft/` (an `extract` file in the plan). 4 download at a time.
  - If the build has a `forge` version, it's recorded as the loader. Otherwise a note explains that the loader ships as files.
- **Single zip:** the zip is downloaded and unpacked into `minecraft/`.
  - There's no hash to check.
  - A note says the loader isn't declared. When the pack is played, the loader is detected from `bin/version.json` or the `version.json` inside `bin/modpack.jar` (see [launching.md](launching.md)).

## Verification
Checked against the live API with a scratch harness:
- **Search, filter and sort:** trending (20 packs), search, the `1.12` filter, and **Most installed**.
- **Solder install:** Tekkit (`tekkitmain`) build 1.2.9i, with 63 mod zips unpacked into `bin/`, `config/` and `mods/`.
- **Single-zip install:** Minecraft Breakdown 9.0.0, a 70 MB zip.
- **Sub-page in the app:** the sub-page and its install card were also checked in the running app.

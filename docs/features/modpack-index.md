# Modpack Index

The local modpack list. Discover normally shows live Modrinth results (see [modrinth-search.md](modrinth-search.md)); this index is the **offline fallback**, used when a Modrinth search fails.

## Source
- Loaded once at startup from `resources/index.json` in the data folder, which is found by searching the working directory and the executable's folder and its parents (see [app-shell.md](app-shell.md)).
- Code: `LoadIndex()` in `src/Model/Modpack.cpp`, called from `main()` in `src/main.cpp`.
- There is no remote index or refresh yet; editing the JSON requires restarting the app.

## Format
A JSON array of modpacks:

```json
[
  {
    "id": "fabulously-optimized",
    "name": "Fabulously Optimized",
    "description": "Beautiful graphics, speedy performance and familiar features in a simple package. Fabric, Minecraft 26.2.",
    "version": "14.1.1",
    "files": [
      {
        "name": "Fabulously.Optimized-v14.1.1.mrpack",
        "url": "https://cdn.modrinth.com/data/1KVo5zza/versions/uQxD0ENB/Fabulously.Optimized-v14.1.1.mrpack",
        "size": 192991
      }
    ]
  }
]
```

| Field | Type | Used for |
|---|---|---|
| `id` | string | Folder name under `downloads/<id>/` and `installs/<id>/` |
| `name` | string | Display name; searched on Discover (case-insensitive) |
| `description` | string | Detail pane text; searched on Discover (case-insensitive) |
| `version` | string | Shown in list and detail pane |
| `files[].name` | string | Display name and download filename: `downloads/<pack id>/<name>` |
| `files[].url` | string | Download source; also the key for download state |
| `files[].size` | integer (bytes) | Shown as a formatted size; not used to verify downloads |

## Data types
`Modpack` and `PackFile` structs in `src/Model/Modpack.h`.
- Local packs have `source = PackSource::LocalIndex`.
- Online packs also carry:
  - `source`
  - `sourceId` and `sourceVersionId`: the source's own ids
  - `websiteUrl`
  - `sourceUrl` and `sourceIsSolder` (Technic only)
- `PackSourceName()` and `PackSourceKey()` give a source's display name and its key in saved JSON (`modrinth`, `curseforge`, `ftb`, `technic`, `atlauncher`, `local`). See [pack-sources.md](pack-sources.md).

## Behavior notes
- Missing fields default to empty strings / `0`.
- If the file is missing, isn't a JSON array, or fails to parse, the result is an empty list (packs parsed before the error are kept).
- The bundled index contains one real pack: the Fabric "Fabulously Optimized" modpack, served from the Modrinth CDN. Its file is an `.mrpack` (a zip holding `modrinth.index.json` and an `overrides/` folder). Extracting it only unpacks the manifest and `overrides/`. The mods listed in the manifest are downloaded when the pack is played (see [launching.md](launching.md)).

# Installation (Zip Extraction)

Turning a downloaded file into extracted pack files.

Whole-pack installs from CurseForge and the other plan-based sources use a different layout, `installs/<pack id>/minecraft/` plus `pack.json`; see [pack-sources.md](pack-sources.md).

## Trigger
The **Install files** button appears once a file has downloaded, both on Discover (see [discover.md](discover.md)) and on the Downloads page (see [downloads.md](downloads.md)). Both call `startInstall(url, pack, zipPath)` in `src/main.cpp`, which starts a worker thread that sets `installing` and then updates `installProgress`, `installed`, and `installError` in the shared download state (see [downloads.md](downloads.md)).

## Extraction (`src/Storage/Installer.{h,cpp}`)
- `InstallZip(zipPath, targetDir, progress, err, cancel)` uses miniz. It is `ExtractZip` with no prefix.
- `ExtractZip(zipPath, targetDir, prefix, progress, err, cancel)` only extracts entries under `prefix` (e.g. `overrides/`), with the prefix removed from their paths. Install plans use it for pack overrides and extract-type files (see [pack-sources.md](pack-sources.md)).
- `ReadZipEntry(zipPath, name, out, err)` reads one entry, such as a manifest, into memory.
- Target directory: the version's install folder (see [library.md](library.md#installed-versions)), e.g. `installs/<pack id>@<version>/`. `startInstall` empties it first, so files from an earlier extraction don't linger.
- Iterates over every zip entry. Directory entries are created, and file entries get their parent folders created before extraction.
- Reports `progress(totalEntries, extractedEntries)` after each entry.
- Takes an optional `cancel` flag (`const std::atomic<bool>*`), checked before each entry. Cancelling stops with `err = "Cancelled"` and leaves already-extracted files in place. The app sets the flag on shutdown.
- Entries whose stats can't be read are skipped.
- The first entry that fails to extract stops the install. `err` is set to `"Failed to extract file: <name>"`.
- A file that isn't a valid zip fails with `"Failed to open zip file"`.

## Behavior notes
- Every downloaded file is treated as a zip; other formats aren't handled.
- Existing files in the target folder are overwritten. Nothing is cleaned up beforehand.
- **Path safety (zip-slip):** `IsSafeEntryPath()` (exposed as `IsSafeRelativePath()` for paths from pack manifests) rejects entries with a drive or root (`C:/x`, `/x`) and entries that climb out of the target after normalization (`../x`, `a/../../x`). The first unsafe entry fails the whole install with `"Unsafe path in zip: <name>"`. Entries that stay inside the target (`a/b/../c`) extract normally.
- Extracted files are written through `OpenFile`/`LongPath` (`src/Util/Paths`), so entry paths past Windows' 260-character limit extract normally. The zip itself is still opened by miniz with a plain path.
- Successfully extracted packs are recorded in `library.json` and stay in My Library (see [library.md](library.md)). After a restart, a saved download whose pack is in the library shows as "Extracted", and other saved downloads offer **Install files** (see [downloads.md](downloads.md)).

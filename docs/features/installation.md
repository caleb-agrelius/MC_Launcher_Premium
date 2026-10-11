# Installation (Zip Extraction)

Turning a downloaded file into extracted pack files.

## Trigger
The **Install files** button appears once a file has downloaded, both on Discover (see [discover.md](discover.md)) and on the Downloads page (see [downloads.md](downloads.md)). Both call `startInstall(url, pack, zipPath)` in `src/main.cpp`, which starts a worker thread that sets `installing` and then updates `installProgress`, `installed`, and `installError` in the shared download state (see [downloads.md](downloads.md)).

## Extraction (`src/Storage/Installer.{h,cpp}`)
- `InstallZip(zipPath, targetDir, progress, err, cancel)` uses miniz.
- Target directory: `installs/<pack id>/`.
- Iterates over every zip entry. Directory entries are created, and file entries get their parent folders created before extraction.
- Reports `progress(totalEntries, extractedEntries)` after each entry.
- Takes an optional `cancel` flag (`const std::atomic<bool>*`), checked before each entry. Cancelling stops with `err = "Cancelled"` and leaves already-extracted files in place. The app sets the flag on shutdown.
- Entries whose stats can't be read are skipped.
- The first entry that fails to extract stops the install. `err` is set to `"Failed to extract file: <name>"`.
- A file that isn't a valid zip fails with `"Failed to open zip file"`.

## Behavior notes
- Every downloaded file is treated as a zip; other formats aren't handled.
- Existing files in the target folder are overwritten. Nothing is cleaned up beforehand.
- **Path safety (zip-slip):** `IsSafeEntryPath()` rejects entries with a drive or root (`C:/x`, `/x`) and entries that climb out of the target after normalization (`../x`, `a/../../x`). The first unsafe entry fails the whole install with `"Unsafe path in zip: <name>"`. Entries that stay inside the target (`a/b/../c`) extract normally.
- miniz opens files with `fopen`, so Windows' 260-character path limit applies. A deep working directory combined with long entry paths, like those in `.mrpack` overrides, can fail with `"Failed to extract file: ..."`.
- Successfully extracted packs are recorded in `library.json` and stay in My Library (see [library.md](library.md)). After a restart, a saved download whose pack is in the library shows as "Extracted", and other saved downloads offer **Install files** (see [downloads.md](downloads.md)).

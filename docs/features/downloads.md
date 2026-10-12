# Downloads

Downloading pack files, tracking their state, and the Downloads page.

## HTTP download (`src/Network/HttpClient.{h,cpp}`)
- `HttpClient` wraps libcurl. Its constructor and destructor call `curl_global_init` and `curl_global_cleanup`, and `main()` owns a single instance.
- Every request sends `User-Agent: MC_Launcher_Premium/0.1` and has a 20 s connect timeout. `Get()` and `Post()` (used for API calls), extra headers, and per-thread connection reuse are covered in [modrinth-search.md](modrinth-search.md).
- `DownloadToFile(url, outPath, progress, err, cancel, headers)`:
  - Streams the response to `outPath`.
  - Follows redirects and treats HTTP status 400 and above as failure.
  - Percent-encodes characters that aren't legal in URLs, such as spaces, which some pack manifests contain. Existing `%XX` escapes are kept.
  - Gives up if the transfer stalls (under 1 byte/s for 60 s); this applies to every request.
  - Writes through `OpenFile`, so output paths may pass 260 characters.
  - Reports `progress(totalBytes, downloadedBytes)` through curl's xferinfo callback (libcurl 7.32 or newer).
  - Takes an optional `cancel` flag (`const std::atomic<bool>*`). When the flag becomes true, the progress callback aborts the transfer and `err` becomes `"Cancelled"`. libcurl calls the callback at least once a second, even when no data is arriving.
  - On failure, sets `err` to `"HTTP <status>"`, the curl error string, or `"Cancelled"`, deletes the partial file, and returns `false`.

## Where files go
`downloads/<pack id>/<file name from the index>`. The worker creates the pack folder, so same-named files in different packs don't collide. Download state is keyed by URL, so two packs that list the **same URL** share one download state.

## Whole-pack installs
Sources that install from a plan (see [pack-sources.md](pack-sources.md)) use one entry keyed `pack:<pack id>`, with `planInstall` set. The other fields used are:
- `active`, `error` and `installed` for the install as a whole
- `stage`, `stageDone` and `stageTotal` for progress
- `blocked` (files to download by hand) and `notes`

The Downloads page shows the stage with its counts and a progress bar, then "Install failed: ...", "Installed", or "Installed - N file(s) need a manual download (see Discover)". These entries aren't saved to `downloads.json`.

## Download state (`src/main.cpp`)
- `std::map<std::string, DownloadState> downloads` is keyed by file **URL** and guarded by `downloadsMutex`.
- `DownloadState` fields:
  - Download: `fileName` and `pack` (a copy of the pack, for its name and icon; both set when the download starts), `active`, `total`, `now`, `outPath` (set on success), `error`
  - Extraction: `installing`, `installProgress` (0–1), `installed`, `installError`
- Each download runs on a background thread started with `StartWorker()`. Each `Worker` has a `done` flag, and `ReapFinishedWorkers()` joins and removes finished workers at the start of every frame.
- On exit, `main()` sets `shuttingDown`, which cancels in-flight downloads and extractions, then joins the remaining workers. There is no per-download cancel button.
- The UI copies state under the lock, then draws from the copy.

## Downloads page (`Page::Downloads`)
Cards use the same measured height, padding, 56px icons and heading-size pack names as My Library. Their heights are retained while offscreen and when revisiting the page. Long text wraps, and actions move to a new row when necessary.

Shows one card per entry in `downloads`, with the pack icon, the pack name, and the file name (`fileName`). Each card shows one of these statuses:
- "Downloading..." plus bytes so far / total and a thin progress bar, once the total is known
- "Download failed: ..."
- "Extracting files..." plus a thin progress bar
- "Extraction failed: ..."
- "Files extracted"
- "Downloaded - ready to extract."

A finished download that isn't extracted or extracting also gets an **Install files** button. This includes one whose extraction failed. It calls the same `startInstall()` as Discover (see [installation.md](installation.md)), so a saved file can be installed even when Discover now lists a newer version of the pack.

If nothing has been started, the page shows an empty state with a **Browse modpacks** button that opens Discover.

While any download or extraction is running, the sidebar's **Downloads** entry shows a count badge. When one finishes, a toast says whether it succeeded (see [ui-theme.md](ui-theme.md)). No toast is shown for transfers cancelled at shutdown.

## Saved downloads (`downloads.json`)
- When a download succeeds, the worker posts `saveDownloads()` to the `UiQueue`. That call copies every entry with a non-empty `outPath` under the lock and writes them to `downloads.json` with `SaveDownloads` (`src/Storage/Library.{h,cpp}`). Each record holds the URL, `fileName`, `outPath`, and a copy of the pack.
- At startup, after My Library loads, `LoadSavedDownloads` restores those entries into `downloads`. Entries whose file is no longer on disk are skipped. A restored entry is marked `installed` when its pack is in My Library (see [library.md](library.md)). Otherwise, the Downloads page shows **Install files** for it, and Discover shows **Install** (which extracts the saved file) while the pack's listed file has the same URL.
- Progress, failed downloads, and extraction errors aren't saved, so after a restart only finished downloads reappear.

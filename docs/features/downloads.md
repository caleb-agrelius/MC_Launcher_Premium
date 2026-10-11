# Downloads

Downloading pack files, tracking their state, and the Downloads page.

## HTTP download (`src/Network/HttpClient.{h,cpp}`)
- `HttpClient` wraps libcurl. Its constructor and destructor call `curl_global_init` and `curl_global_cleanup`, and `main()` owns a single instance.
- `DownloadToFile(url, outPath, progress, err, cancel)`:
  - Streams the response to `outPath`.
  - Follows redirects and treats HTTP status 400 and above as failure.
  - Reports `progress(totalBytes, downloadedBytes)` through curl's xferinfo callback (libcurl 7.32 or newer).
  - Takes an optional `cancel` flag (`const std::atomic<bool>*`). When the flag becomes true, the progress callback aborts the transfer and `err` becomes `"Cancelled"`. libcurl calls the callback at least once a second, even when no data is arriving.
  - On failure, sets `err` to the curl error string (or `"Cancelled"`), deletes the partial file, and returns `false`.

## Where files go
`downloads/<pack id>/<file name from the index>`. The worker creates the pack folder, so same-named files in different packs don't collide. Download state is keyed by URL, so two packs that list the **same URL** share one download state.

## Download state (`src/main.cpp`)
- `std::map<std::string, DownloadState> downloads` is keyed by file **URL** and guarded by `downloadsMutex`.
- `DownloadState` fields:
  - Download: `fileName` (set when the download starts), `active`, `total`, `now`, `outPath` (set on success), `error`
  - Extraction: `installing`, `installProgress` (0–1), `installed`, `installError`
- Each download runs on a background thread started with `StartWorker()`. Each `Worker` has a `done` flag, and `ReapFinishedWorkers()` joins and removes finished workers at the start of every frame.
- On exit, `main()` sets `shuttingDown`, which cancels in-flight downloads and extractions, then joins the remaining workers. There is no per-download cancel button.
- The UI copies state under the lock, then draws from the copy.

## Downloads page (`Page::Downloads`)
Shows one card per entry in `downloads`, labeled with the file name (`fileName`). Each card shows one of these statuses:
- "Downloading..." plus a progress bar once the total is known
- "Download failed: ..."
- "Extracting files..." plus a progress bar
- "Extraction failed: ..."
- "Files extracted"
- "Downloaded - open Discover to extract this pack."

If nothing has been started, the page shows a message saying so. Download state is in memory only and resets when the app restarts.

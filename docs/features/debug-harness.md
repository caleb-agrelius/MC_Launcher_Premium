# Debug harness

Prints what the launcher is doing to the console as it happens: API requests, file changes, child processes and `app.log` lines. It is off unless switched on, and it changes nothing about how the launcher behaves. Code: `src/Util/DebugTrace.*`, `src/Util/TracedFs.*`, `src/Network/TracingHttpClient.*`.

## Turning it on

| How | Effect |
|---|---|
| `mcdebug debug` | Builds, then runs the Debug build in the current console with every channel except `body` on |
| `mcdebug debug http,file` | Same, with only the listed channels (spaces or commas between them) |
| `MC_Launcher_Premium.exe --debug` | All channels except `body` |
| `MC_Launcher_Premium.exe --debug=http,body` | Only the listed channels |
| `MCLP_DEBUG=file,process` (environment) | Same list syntax; `1` or empty means all. `--debug` on the command line adds to it |

The app is a GUI-subsystem executable (no console window of its own, in every build configuration). At startup Platform::AttachParentConsole() joins the terminal that launched it, if any, so the trace prints there; mcdebug debug and mcdebug offline use start /b /wait so the batch file waits for the app. Started from Explorer, nothing is shown. An unknown channel name prints a warning to the console and is ignored. `mcdebug run` still starts the app in its own window with the harness off.

## Offline mode

Debug launches that don't need the network shouldn't call the pack APIs on every start (Discover searches four sources at once the moment it opens). Offline mode makes every request fail at once and never reaches the network, so there is nothing to rate-limit or to mistake for a bug in the app.

| How | Effect |
|---|---|
| `mcdebug offline` | Builds, then runs the Debug build in this console, offline, with the `http` and `log` channels (blocked requests show as `FAILED: Offline mode...`). `mcdebug offline file` picks other channels |
| `MC_Launcher_Premium.exe --offline` | Offline, harness as configured |
| `MCLP_OFFLINE=1` (environment) | Same; `0`, `false` or empty mean online |

**Use offline for any launch or test that isn't about something from the network or a third-party API:** layout and UI work, settings, Library, Manage, installed packs, dialogs, the UI test harness. Launch online only when the change touches an API client, search, version lists, image downloads, sign-in or downloads, and then prefer `mcdebug debug http` so the requests are visible.

In the app, `OfflineHttpClient` (`src/Network/OfflineHttpClient.*`) stands in for the real client in `AppRoot`, under the same `TracingHttpClient`, so callers just see a failed request with the message "Offline mode: network requests are disabled (started with --offline)". `Debug::OfflineRequested` reads the flag and the variable, and `app.log` notes "Offline mode" at startup. `MCLP_OFFLINE_DELAY_MS=n` makes each refusal wait `n` ms first (a cancel ends the wait), which holds the app in its loading states so the placeholders and spinners can be looked at or screenshotted. The UI test harness (`tests/UiDriver.ps1`) starts the app offline unless a test sets `$script:AppOnline = $true`.

## Channels

| Channel | Shows |
|---|---|
| `http` | Each request that goes out as `GET`, `POST` or `DOWNLOAD` with its URL, then `<-` the outcome (`ok`, `HTTP 403` or `FAILED: reason`), the size and the time in milliseconds. A GET answered from the response cache shows as `CACHE <url> (N bytes, no request)`, and a Discover refresh as `CACHE cleared` (see [request-caching.md](request-caching.md)) |
| `body` | The first 400 characters of each GET response, on one line. Not part of `all`, because bodies are large |
| `file` | `mkdir`, `copy`, `copy-tree`, `rename`, `remove`, `remove-all` (with the item count), `hard-link`, `write` (the files the launcher writes itself, such as `settings.json`, `library.json`, markers and pack files) and `extract` / `extracted` for a zip. A failed operation ends in `FAILED: reason`. `mkdir` shows only when a folder was created and `remove` only when something was removed |
| `process` | `start` with the command line and working folder, then `exit <code>` (or `killed` / `left running` after a stop or cancel), for Java, installers and servers |
| `log` | Every line the launcher writes to `app.log`, as it is written. `app.log` itself is unchanged |
| `perf` | Not part of `all`. Every 5 seconds: the frame rate and the average milliseconds per frame spent polling events, building the UI (with the mascot's share), rendering and swapping buffers, e.g. `240.1 fps \| ms per frame: poll 0.006, ui 0.082 (mascot 0.027), render 0.088, swap 0.026, total 4.165`. The timers (`src/Util/FramePerf.*`) cost one atomic load when the channel is off. `tests/CpuProbe.ps1` runs it while measuring the process's CPU time |

## Output format

```
14:01:24.420 [HTTP ] t1  GET  https://api.modrinth.com/v2/search?...
14:01:24.510 [HTTP ] t1    <- GET ok, 56547 bytes, 90 ms  https://api.modrinth.com/v2/search?...
14:01:30.113 [FILE ] t0  remove-all C:\...\installs\fabulously-optimized@14.1.1 (74 item(s))
```

Each line has the local time, the channel, and the thread (`t0` is the first thread to trace, normally the UI thread; each worker has its own number, so a request and its result share one). The trace goes to **stderr**, so build output and the trace stay apart: `mcdebug debug 2> trace.log` saves it.

## What is never shown

- Secret query values (names containing `key`, `token`, `secret`, `code` or `password`) print as `***`.
- Request headers show by name only (`[x-api-key]`), never the value.
- Request bodies and POST/sign-in responses are never printed, only their sizes.
- The value after a `--accessToken` or similar flag in a command line prints as `***`.

## How it works

- `Debug` (`Util/DebugTrace.h`) holds the enabled channels (an atomic mask) and writes lines under a mutex, so any thread can trace. A trace call when its channel is off costs one atomic load. `LogTee` wraps the `app.log` stream in `main()` when the `log` channel is on.
- `TracingHttpClient` implements `IHttpClient` around the real client. `AppRoot` hands it to every service and the image cache, so no caller changed. With the `http` channel off it passes each call straight through.
- `TracedFs` wraps the `std::filesystem` calls that change the disk (`Remove`, `RemoveAll`, `Rename`, `CopyFile`, `Copy`, `CreateDirectories`, `CreateHardLink`) and `OpenWrite`, which opens an `ofstream` and notes the write. New code that changes files uses these instead of `std::filesystem` and `std::ofstream` directly so it shows in the trace. Reads and directory listings are not traced.
- `Util/Process.cpp` traces each process it starts, and `Storage/Installer.cpp` traces zip extraction (one line per archive, not per entry).

`launcher_checks` covers channel parsing, redaction, the log tee, `TracedFs`, the HTTP decorator (against a fake client) and process tracing.

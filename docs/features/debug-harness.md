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

An unknown channel name prints a warning to the console and is ignored. `mcdebug run` still starts the app in its own window with the harness off.

## Channels

| Channel | Shows |
|---|---|
| `http` | Each request as `GET`, `POST` or `DOWNLOAD` with its URL, then `<-` the outcome (`ok`, `HTTP 403` or `FAILED: reason`), the size and the time in milliseconds |
| `body` | The first 400 characters of each GET response, on one line. Not part of `all`, because bodies are large |
| `file` | `mkdir`, `copy`, `copy-tree`, `rename`, `remove`, `remove-all` (with the item count), `hard-link`, `write` (the files the launcher writes itself, such as `settings.json`, `library.json`, markers and pack files) and `extract` / `extracted` for a zip. A failed operation ends in `FAILED: reason`. `mkdir` shows only when a folder was created and `remove` only when something was removed |
| `process` | `start` with the command line and working folder, then `exit <code>` (or `killed` / `left running` after a stop or cancel), for Java, installers and servers |
| `log` | Every line the launcher writes to `app.log`, as it is written. `app.log` itself is unchanged |

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

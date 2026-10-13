# Architecture

How the source is divided, what may depend on what, and where threads and lifetimes are decided. The folder-by-folder list is in [project-layout.md](project-layout.md).

## Layers

```
main.cpp          finds the data folder, opens app.log, runs Application
App/              the window, the main loop, and the composition root (AppRoot)
UI/               pages, dialogs, the sidebar and shell, shared widgets (Dear ImGui)
Services/         what the launcher does: library, downloads, play, servers, updates, discover, settings
Game/ Network/    Minecraft, loaders, servers, pack-source clients
Storage/ Model/   files on disk and the data structures they hold
Core/             framework-free infrastructure and the interfaces everything else depends on
Platform/         the operating system, one file per OS
Util/             small helpers (text, hashes, paths, zip, processes)
```

Each layer uses the ones below it and never the ones above.

- **Services have no Dear ImGui.** A service holds state, runs work and reports through interfaces. A test can build one with fakes.
- **Pages have no threads and no I/O.** They read a service, draw it, and call a service when a button is pressed. Slow work is always a service's.
- **Only `App/` and `Platform/` include GLFW or OpenGL, and only `Platform/` includes OS headers in new code.** `App/`, `Core/`, `Services/`, `UI/Shell`, `UI/Pages` and `UI/Dialogs` have no `#ifdef _WIN32`. A few older files still have one where the standard library has no portable form: `Util/Process` (a Windows and a POSIX implementation), `Game/JavaFinder` (the registry), `Game/GameFiles` and `Game/Launcher` (the OS name and classpath separator), `Util/Paths` (long paths), `Util/DebugTrace`, `Util/Text` and `Network/Ftb` (`localtime_s` and `gmtime_s`), `Game/InstanceContent` (a wide `fopen`) and `UI/Theme` (font files). Microsoft sign-in and the saved account, which had OS code of their own, now use `Platform` only. See [platforms.md](features/platforms.md).

## Interfaces (dependency inversion)

Everything a service or page needs from "the rest of the app" is an abstract class in `Core/` or next to its users, and `App/AppRoot` is the only place that names the concrete class behind it.

| Interface | What it stands for | Implemented by |
|---|---|---|
| `ITaskRunner` | run this off the UI thread | `WorkerPool` |
| `IUiPoster` | run this on the UI thread next frame | `UiQueue` |
| `IHttpClient` | the network | `HttpClient` (libcurl), wrapped by `TracingHttpClient` for the debug harness, and for browsing services also by `CachingHttpClient` |
| `ICacheControl` | empty the response cache (Refresh) | `CachingHttpClient` |
| `INotifier` | a message for the player | `ToastNotifier` (toasts) |
| `IWindowControl` | minimize, restore, close the launcher | `GlfwWindow` |
| `IDisplayMetrics` | the monitor's DPI scale | `ThemeController` |
| `INavigator` | go to a page, a pack's Manage page, a pack's server | `AppShell` (through `NavigatorProxy`) |
| `IDialogs` | open a dialog by name | `DialogHost` |
| `IInstallRequester` | install this pack (maybe after asking about optional mods) | `OptionalModsService` |
| `IPage`, `IPackPage` | a screen the sidebar leads to | each page |

`Runtime` (`Core/Runtime.h`) bundles the first five plus the log and the shutdown flag. Every service takes one. `AppRoot` has two: the normal one, and `m_browseRt` whose `http` is the caching client, for `DiscoverService` and `PackDetailsService` (see [request-caching.md](features/request-caching.md)).

## Threads and lifetimes

- Everything runs on the UI thread except worker tasks. A worker hands its result back with `IUiPoster::Post`, so service state is only touched on the UI thread. The exceptions are guarded by a mutex: the downloads map, the plays map, and a server's setup fields.
- Each service owns a `TaskScope` (declared last in the class). Tasks started through it may use `this`, because its destructor waits for them. That is what keeps a worker from outliving what it works on.
- Members of `AppRoot` are declared in dependency order: the worker pool and network first, then services, then pages. C++ destroys them in reverse, so pages go first, then services (waiting for their tasks), then the pool.
- Shutdown (`AppRoot::Shutdown`): running servers get `stop`, the shutdown flag is set (long tasks check it), workers are joined, and the UI queue is drained one last time so finished work is saved. Then settings are written with the window's last placement.
- Logging is a `std::ostream&` and is used only from the UI thread.

## Pages and dialogs

`AppShell` draws the sidebar, then the current `IPage` under its header (`DrawHeader`, `Draw`), then the dialogs. It doesn't know what a page contains. A page is either self-contained (Discover, Library, Downloads, Servers) or a *host* that adapts an older view to the services (`ManagePageHost` for `ManagePage`, `SettingsPageHost` for `SettingsPage`).

Big pages are split into parts that each own a region of the screen, e.g. Discover is `DiscoverFilterBar`, `DiscoverResultList` and `DiscoverDetails` (which is `PackInstallSection`, `PackPlayButton` and `PackInfoTabs`); Servers is `ServerList` and `ServerDetails` (`ServerFilesCard`, `ServerRunCard`, `ServerConsole`, and the Settings, Mods and Docker tabs). Per-screen view state (the open tab, a half-typed field) lives in the part that draws it, not in the service.

## Adding things

- **A new service:** put it in `Services/<Area>/`, take a `Runtime` (and the services it uses) in its constructor, own a `TaskScope` if it runs tasks, add it to `AppRoot` after what it depends on.
- **A new page:** implement `IPage` in `UI/Pages/<Page>/`, take the services it draws, add it to `AppRoot` and to `AppShell::Pages`, and add a `Page` value and sidebar link.
- **A new OS call:** declare it in `Platform/Platform.h`, define it in `Platform_win32.cpp` and `Platform_posix.cpp`.

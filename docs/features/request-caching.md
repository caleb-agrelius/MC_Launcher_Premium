# Request caching

Browsing Discover used to ask the pack APIs again for the same data every time: leaving the page and coming back, toggling a source chip, or reopening a pack all sent fresh requests. `CachingHttpClient` (`src/Network/CachingHttpClient.*`) is an `IHttpClient` that remembers successful GET responses in memory, so repeats are answered without touching the network.

## Rules
- **Key:** the URL plus the extra headers (so a different API key is a different entry). Keys live only in memory.
- **Lifetime:** 15 minutes (`Limits::lifetimeSeconds`). An older entry is fetched again and replaced.
- **Only successful GETs are kept.** A failure is never remembered, so retrying always sends a request. `Post`, `Request` and `DownloadToFile` pass straight through.
- **Shared requests:** identical GETs made at the same moment (several workers, or a chip toggled quickly) send one request; the others wait for it and read its result. A waiter that is cancelled gives up without waiting. If the first request fails, the next waiter tries for itself.
- **Limits:** a response over 8 MB (`maxEntryBytes`) is returned but not kept; past 64 MB in total (`maxTotalBytes`) the oldest entries go first.
- **Threads:** safe to call from any worker. `Clear()` also discards responses that were on their way when it was called, so an old answer can't be stored after a refresh.

## Where it is used
`AppRoot` builds the client around `TracingHttpClient` and gives it, through a second `Runtime` (`m_browseRt`), only to the services that read pack data to show it: `DiscoverService` (searches, Modrinth categories) and `PackDetailsService` (a pack's latest version, banner, description, mod list, version list). Everything where stale data would be wrong stays on the uncached client: installs and install plans, update checks (`UpdateService`), Play, servers, sign-in, optional-mod lists, Manage > Content, and image downloads (`ImageCache` keeps its own textures).

Because the cache is outside the tracing client, `mcdebug debug http` shows only the requests that really went out, plus a `CACHE <url> (N bytes, no request)` line for each one answered from memory and `CACHE cleared` on a refresh.

## Refresh
The **Refresh** button on the Discover filter row (left of the list/grid switch; disabled while a search is waiting or running) calls `DiscoverService::Refresh()`: it clears the cache (`ICacheControl::Clear`), drops the pack lists FTB and ATLauncher keep in memory for the whole run (`ClearSourceCaches`), clears `PackDetailsService`'s fetched descriptions, mod lists, version lists, download sizes and errors, reloads the category list if it was loaded, and sends the current search again. It is the only way to bypass the cache before an entry expires.

## Not cached
The cache is in memory, so it starts empty on every launch. Keeping it on disk is listed in [planned.md](planned.md).

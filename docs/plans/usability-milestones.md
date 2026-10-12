# Usability fixes: milestones

Branch `dev/usability-fixes`, cut from `main` at `7ac9766`. The branch started with the uncommitted
`.disabled` server-mod change that was already in the working tree (`src/Game/ServerSetup.cpp`,
`docs/features/servers.md`).

Each milestone counts as done only after it builds, `launcher_checks` passes, and its feature doc is updated.
Milestones are verified stages, not commits.

## Found along the way

- **Long override paths:** server setup failed for a Modrinth pack whose overrides nest past 260 characters (`recursive_directory_iterator ... cannot find the path specified`). `LongPath` only prefixes paths that are already long, so a short root walked recursively still overflowed. Added `ExtendedPath` (always `\\?\`), used for server override copying, whole-pack copying and `RemoveServer`, with a 280-character override check. Play's `PrepareInstance` wasn't changed and may have the same limit.
- **`FindModrinthClientOnly` crash:** `live_checks` caught a range-for over `json::parse(...).items()`, a dangling temporary. Fixed before it shipped.
- **`tests/WindowSmoke.ps1` fails on this PC with "Launcher did not close normally"**, but it also fails with a clean build of `main` (built in a scratch worktree), so it predates this branch. Launching, resizing and closing the same way by hand exits in about 0.1 s, so the cause is in how the harness starts the hidden window. Not investigated further.

| # | Report | Status | Verification |
|---|---|---|---|
| M1 | Can't remove server installs natively | Done | `RemoveServer` check: a 300+ character nested folder is deleted, and a drive root is refused. Build passes. |
| M2 | Can't change host IP | Done | Checks: `server-ip` round-trips through `server.properties`. `IsValidServerIp` accepts and rejects the expected cases. Every adapter address on this PC is a usable IPv4 address. Build passes. |
| M3 | Disable individual mods for a server | Done | Checks: mods are listed and switched off and on again. An update neither re-downloads a disabled pack mod nor re-copies a disabled override mod. Unsafe names are refused. Build passes. |
| M4 | Mod list for a modpack in the Discover tab | Done | `live_checks` against the live APIs: Modrinth (Fabulously Optimized, 51 mods), FTB (StoneBlock 4, 414), ATLauncher (All The Forge 10, 160), Technic Solder (Tekkit Legends, 57). CurseForge was not run because no API key is available. It shares the install's zip download. Build and `launcher_checks` pass. |
| M5 | Server hosting for non-Modrinth packs | Done | Check: a whole-pack fixture copies mods and config but skips saves and `options.txt`. Source-flagged and Modrinth-flagged mods arrive `.disabled`. An update keeps server config and a re-enabled mod, and notes a failed lookup. `live_checks` confirms the Modrinth lookup flags Sodium and not Lithium. That run caught and fixed a dangling-temporary crash. Build passes. |
| M6 | docker-minecraft-server support | Done | Checks: image tags per Java and Minecraft version, compose contents, and refusal with a Host IP set. `docker compose config` validates a generated file. All five itzg tags and the env variable names were confirmed against Docker Hub and itzg's docs. Not run: `docker compose up`, because the Docker engine on this PC isn't running. Build passes. |
| M7 | Descriptions for modpacks | Done | Checks: the Markdown and HTML converter, with exact expected output for headings, links, images, lists, tables, entities and script removal. `ShortenText`. `live_checks` fetched and converted descriptions for Modrinth, FTB, ATLauncher and Technic with no leftover tags or link syntax. CurseForge was not run (no API key). Build passes. |
| M8 | Download and install in one step | Done | In the running app (isolated data folder), one click on **Install** for Fabulously Optimized downloaded and extracted it, added it to My Library, and enabled PLAY. Build and `launcher_checks` pass. |
| M9 | Dialogs don't follow the selected theme (reported mid-task) | Done | Every ImGui color slot is now themed (title bars, modal dimming, tabs, links...). `ThemeColorChecks` fails if any built-in theme leaves a visible slot at ImGui's stock value. In the app, the Uninstall dialog under Nether shows a themed title bar and dimming. |

## Merge with main (version picker, #8)

`origin/main` (`097da89`) was merged in. It installs several pack versions side by side (`installs/<id>@<version>/`) and adds a version picker for Discover and servers. Integration changes beyond the conflict resolutions:
- Server hosting checks (`canHostServer`, the Servers list and details) use the active version's install folder instead of `installs/<pack id>`.
- Choosing a non-installed version of a CurseForge, FTB, Technic or ATLauncher pack for a server shows "Install <version> from Discover first" instead of quietly using the active version (only Modrinth versions can be downloaded for a server).
- Main's dropped-mod cleanup on version change also removes a dropped mod's `.disabled` copy.
- **Also delete its server** is offered only when an uninstall removes the whole pack, not one of several versions.

Verified after the merge: build, `launcher_checks` and `live_checks` pass. The merged app starts with an older `library.json` and shows the Pack version picker next to Host IP on the Servers page.

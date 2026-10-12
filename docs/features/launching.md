# Launching (Play)

Turning an installed modpack into a running Minecraft. Entry point: `PlayModpack()` in `src/Game/Launcher.{h,cpp}`, started by `startPlay()` in `src/main.cpp`.

## Where to click
- **My Library:** each pack card has a **PLAY** button, for packs from every source. While the pack is being prepared, the card shows the current step, a spinner, a progress bar when the size is known, and a **Cancel** button. While the game runs it shows "Minecraft is running" and a **Stop game** button, which asks for confirmation and then force-closes Minecraft (unsaved progress since the last autosave is lost). Errors appear in red on the card.
- **Discover:** the big **PLAY** button works once the selected pack is in My Library. It starts the same play flow and switches to My Library. Otherwise it's disabled, with a hint to install the pack's files.
- An account is required (see [accounts.md](accounts.md)). Pressing Play without one shows an error on the card.

## Steps (`PlayModpack`)
`PlayModpack` has two front ends that share the remaining steps (`InstallAndLaunch`):
Play uses the pack's active installed version: its install folder (`PlayRequest::extractedDir`) and game folder (`instanceDir`), see [library.md](library.md#installed-versions).
- **`.mrpack` packs** (Modrinth and the local index): steps 1 and 2 below.
- **Whole-pack installs** (CurseForge, FTB, Technic, ATLauncher), recognized by a `pack.json` in the install folder (`IsWholePackInstall`). The install plan already filled the game folder, `<install folder>/minecraft/`, so steps 1 and 2 are replaced by `ReadInstalledPack()`:
  - It reads the Minecraft version, `loader` and `loaderVersion` from `pack.json`. `ParseLoaderName` maps `forge`, `neoforge`, `fabric` and `quilt` (any case). An unknown loader fails with a message naming it.
  - `NormalizeLoaderVersion` strips the forms sources wrap versions in: a `forge-`/`neoforge-`/`fabric-`/`quilt-` prefix, a leading `<mc>-` and a trailing `-<mc>`. `forge-47.2.0`, `1.20.1-47.2.0` and `47.2.0-1.20.1` all become `47.2.0`.
  - **No loader declared** (Technic single-zip packs): the loader is detected from `minecraft/bin/version.json`, else the `version.json` inside `minecraft/bin/modpack.jar`, by finding the loader's own library (`net.minecraftforge:forge`, `net.minecraftforge:minecraftforge`, `net.neoforged:neoforge` or `forge`, `net.fabricmc:fabric-loader`, `org.quiltmc:quilt-loader`), like Prism's `TechnicPackProcessor`. If none is found, the pack launches as vanilla.
  - A missing game folder fails with "Install it again".

1. **Read the pack:** `ReadMrpackIndex()` (`src/Game/PackInstance.cpp`) reads `modrinth.index.json` from the install folder. It takes the Minecraft version and loader from `dependencies` (`fabric-loader`, `quilt-loader`, `neoforge`, `forge`) and the client files: entries with `env.client == "unsupported"` are skipped, and paths that are absolute or climb out of the instance are rejected.
2. **Prepare the instance:** `PrepareInstance()` downloads the listed files into the game folder. The first time each pack version is prepared, it copies `overrides/` then `client-overrides/` over it. The marker `.mclp-prepared` in the game folder records the version and every file it listed, so later launches keep the player's own option and config changes. When a new pack version is prepared, files the old version listed but the new one doesn't (mods the pack dropped) are deleted first. Files the player added were never listed, so they stay.
   Then, if any game options are turned on in Settings > Game options, `ApplyGameOptions()` writes them into the instance's `options.txt` ("Applying game options"; see [settings.md](settings.md)).
3. **Vanilla Minecraft:** `InstallVanillaJson()` gets `versions/<mc>/<mc>.json` from Mojang's version manifest, and `ResolveVersion()` reads it.
4. **Java:** if Settings > Minecraft names a Java (`launch.javaPath`), it is used instead: a `java.exe` is swapped for the `javaw.exe` next to it, and a folder means its `bin/javaw.exe`. A path that doesn't exist fails with an error naming it. Otherwise, `InstallJavaRuntime()` installs the Mojang Java runtime named by the version's `javaVersion.component` (default `jre-legacy`) into `minecraft/runtime/<component>/`. `.manifest-sha1` records the installed manifest, so later launches skip it. If Mojang can't be reached, an already-installed runtime is used.
5. **Game files:** `InstallGameFiles()` downloads the client jar, libraries, old-style natives, the log4j config, the asset index and assets. Natives are extracted once into `versions/<id>/natives/`, with a `.extracted` marker. Pre-1.7.3 "virtual" asset indexes are also copied by name into `assets/virtual/<index>/`.
6. **Mod loader:** `InstallLoader()` (`src/Game/Loaders.cpp`):
   - **Fabric / Quilt:** fetches the loader's ready-made profile JSON from `meta.fabricmc.net` / `meta.quiltmc.org` into `versions/<id>/`, skipped if it already exists.
   - **Forge / NeoForge:** downloads the official installer into `minecraft/installers/` and reads the version id from `version.json` inside it. Unless that version and all its generated libraries already exist, it runs the installer headless (`--installClient <root>`, retried with `--install-client`) with the runtime's console `java.exe`. Output goes to `minecraft/logs/loader-installer.log`. A `launcher_profiles.json` stub is created because the installer requires it. NeoForge for 1.20.1 uses the old `net.neoforged:forge` artifact. Installers without `version.json` (very old Forge) fail with a clear error.
7. Resolve the loader's version and download its extra libraries.
8. **Launch:** versions whose asset index sets `map_to_resources` (before 1.6) first get their assets copied by name into `<game folder>/resources/`, missing ones only. Then it builds the command line and runs `javaw.exe` from the game folder, without a console. stdout/stderr are appended to `<game folder>/logs/launcher-output.log`. The worker waits until the game exits. A non-zero exit code is reported on the card.

## Version JSON handling (`src/Game/GameFiles.cpp`)
- `ResolveVersion()` follows `inheritsFrom` and merges from vanilla up. Children override `mainClass`, `type`, `minecraftArguments`, `assetIndex`, `javaVersion` and `logging`, and append to `arguments.jvm`/`arguments.game`. The client jar comes from the version that has `downloads.client` (or `jar`).
- Libraries are taken from the most specific version first and de-duplicated by `group:artifact[:classifier]`, so a loader's copy of a library wins.
- `RulesAllow()` evaluates `rules` for Windows x64. Feature rules (demo, custom resolution, quick play) never match.
- Libraries with `downloads.artifact` use its URL and SHA-1. Libraries without it (Fabric/Quilt style) use `url` (default `libraries.minecraft.net`) plus the maven path. Libraries with an empty URL must already exist, because Forge's installer generates them.

## Downloads
`DownloadAll()` skips files that exist with the expected size and downloads the rest on 8 threads. Each file goes to a uniquely named `<file>.<tag>-<n>.part`, is checked against its SHA-1, then renamed into place. Two packs or two launcher windows fetching the same shared file therefore don't collide, and a file another downloader finished first counts as done. Replacing an existing file (e.g. a server's `server.jar` when its Minecraft version changes) is retried for about 2 seconds, removing the old file halfway, because Windows can keep a just-closed file locked briefly. Each file gets 3 download attempts, and the first failure stops the step. Progress is reported as "<step> (done/total)".

## Command line
- Placeholders (`${auth_player_name}`, `${classpath}`, `${natives_directory}`, `${library_directory}`, `${classpath_separator}`, ...) are substituted. Unknown ones become empty. `user_type` is `msa`.
- Launch settings come from Settings > Minecraft (`Settings::launch`, see [settings.md](settings.md)), copied into `PlayRequest::launch` when Play is pressed.
- JVM arguments, in order: `-Xms<minMemoryMb>M` (only when set), `-Xmx<maxMemoryMb>M` (default 4096), the version's `arguments.jvm` or, for old versions, `-Djava.library.path` and `-cp`, the log4j config argument when the version has one, then the player's own JVM arguments. Those are split on spaces, with double quotes grouping words (`SplitArguments`), and go last so they override the defaults.
- Game arguments: the version's, then `--fullscreen` when fullscreen is on, or `--width`/`--height` when a custom window size is set. The version JSON's own resolution arguments are behind a feature rule that never matches, so nothing is duplicated.
- **When the game starts** (`launch.afterLaunch`), on the "Playing" step: keep the launcher open (and show a "Minecraft started" toast), minimize it (restored when the game exits), or close it. Closing works like closing the window by hand: the game keeps running.
- A play that fails (not cancelled) also shows an error toast.
- For Java 9 and newer, JVM arguments go in `.mclp-jvm-args.txt` in the game folder and are passed as `@file`, to stay under Windows' command-line limit. The main class and game arguments, including the access token, stay on the command line.

## Folders (gitignored, in the data folder)
- `minecraft/`: shared `versions/`, `libraries/`, `assets/`, `runtime/`, `installers/`, `logs/`, `launcher_profiles.json`
- `instances/<pack id>/` or `instances/<pack id>@<version>/`: `.mrpack` game folders (mods, config, saves, logs), one per installed version

## Limitations
- Windows x64 only (process launching and the Java runtime platform).
- Mod files are kept by size, not re-hashed on every launch. **Manage > Repair files** in My Library re-checks them by hash.
- ATLauncher `jar` and `forge` jar mods (saved to `jarmods/` by the install plan) aren't added to the game jar, so very old packs that rely on them may not start.
- Closing the launcher stops preparation but leaves a running game open. Only **Stop game** ends it from the launcher: it sets `PlayRequest::stopGame`, which makes `RunProcess` terminate the process (its `kill` flag) and `PlayModpack` return "Stopped". That shows a "Minecraft was closed" toast instead of an error.

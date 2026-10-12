# Dedicated servers

The **Servers** page hosts installed packs from every source on Windows. My Library's **Server** button opens the pack on that page. A pack can be hosted when it has an extracted `modrinth.index.json` (Modrinth) or a whole-pack install's `pack.json` and `minecraft/` folder (CurseForge, FTB, Technic, ATLauncher). `CanCreateServer` checks this. A pack whose files are missing shows "Pack files are missing" and a hint to repair or reinstall it.

The page has the pack list on the left (320px scaled by default; drag the gap beside it to resize, saved as `paneWidths.servers`) and the selected pack's server on the right.

The server pane has four tabs (`ServerState::tab`, a `Widgets::TabStrip`):
- **Run**: the Server files card, the Run card (EULA, start/stop, join addresses) and the console.
- **Settings**: memory and `server.properties`.
- **Mods (n)**: the mods list, with n the number of mods.
- **Docker**: reads "Docker (on)" while the server runs in a container.

## Setup (`src/Game/ServerSetup.{h,cpp}`)

**Create server files** runs on a background worker and reports progress, errors and a Cancel action. Files go in `servers/<pack id>/`, separate from the singleplayer instance. Setup reads the active version's install folder, or the version picked under **Pack version** (below). The marker `.mclp-server-prepared` records the pack version and every file it listed; when a different version is set up, files the old version listed and the new one doesn't (dropped mods, and their `.disabled` copies) are deleted first.

**Modrinth packs:** the setup reads the pack index with the server environment filter, omitting files marked `server: unsupported`. It copies common overrides and server overrides, with server overrides taking precedence; client overrides are excluded. Existing server configuration and worlds are preserved when **Update server files** runs. A pack file or override with a `<name>.disabled` copy next to it (e.g. `mods/e4mc-fabric-6.2.3-modern.jar.disabled`) is not downloaded or copied again, so mods turned off on the Mods tab stay off.

**Whole-pack installs** (CurseForge, FTB, Technic, ATLauncher): the Minecraft version, loader and pack version come from `pack.json` (`ReadInstalledPack`, which detects Technic loaders from `bin/`). The version's installed game folder (`<install folder>/minecraft/`) is copied into the server folder, except top-level client entries: `saves`, `screenshots`, `logs`, `crash-reports`, `resourcepacks`, `shaderpacks`, `texturepacks`, `bin`, `versions`, `libraries`, `assets`, `natives`, `jarmods`, `downloads`, `.fabric`, `.mixin.out`, and client files such as `options.txt`, `servers.dat` and `usercache.json`.
- All copying uses `\\?\` paths (`ExtendedPath`), as does `CopyOverrides` for Modrinth packs, so configs nested past 260 characters (e.g. `config/modpack_defaults/...`) copy correctly.
- Mod jars under `mods/` are copied when the server doesn't have them yet (or a `.disabled` copy), so updates add new mods without undoing the player's changes. Other files are copied only the first time each pack version is set up, without overwriting existing files. Obsolete mods from an earlier version aren't removed for whole-pack servers.
- Client-only mods are copied as `<name>.jar.disabled`, so the server starts without them and the Mods tab can turn them back on. A mod counts as client-only when the source marked it at install time (`clientOnlyFiles` in `pack.json`: FTB's `clientonly` and ATLauncher's `server: false`), or when Modrinth knows the file by SHA-1 and its project has `server_side: unsupported` (`FindModrinthClientOnly`, called through `ServerSetupRequest::findClientOnly`). Only mods the server doesn't have yet are looked up.
- The player's edits to the pack version (see [manage.md](manage.md#your-changes)) carry over for every source. Mods they added (`ServerSetupRequest::addedMods`) are copied into the server's `mods/` when it doesn't have them, with a setup note naming them. Pack files they removed (`removedFiles`) aren't downloaded. A modified pack shows "modified pack" in the list and "<version> (installed, modified)" under **Pack version**.
- Setup notes are saved in `.mclp-server.json` and shown in the Server files card: how many client-only mods were turned off (with up to five names), or that Modrinth couldn't be reached for the check.
- Packs installed before `clientOnlyFiles` existed rely on the Modrinth lookup alone. FTB server-only files aren't installed on the client, so the server doesn't get them. CurseForge-only client mods that Modrinth doesn't know aren't detected, so turn them off by hand if the server crashes.

**Pack version:** above the buttons, a dropdown (the same version list as Discover, see [discover.md](discover.md)) chooses which version of the pack the server gets. It starts on the pack's active version in My Library, shown as "<version> (installed)", and is disabled while the server runs. Once the server is set up, "Server has <version>" shows what it actually runs. Picking another version that's installed in My Library uses that version's install folder. Picking a Modrinth version that isn't installed (`ServerState::version`) makes **Create/Update server files** download that version's `.mrpack` into `downloads/<pack id>/server/` and unpack it into `servers/<pack id>/.mclp-pack/` (emptied first), then set up from there. Other sources have no single file to download, so picking a version of theirs that isn't installed shows "Install <version> from Discover first" instead of setting up. Modrinth packs installed before versions had ids are matched by version number.

Java comes from Settings > Minecraft: a chosen Java, or Automatic (an installed Java with the major version the Minecraft version needs, else Mojang's runtime, as for PLAY in [launching.md](launching.md)); the server uses its console `java.exe`. Setup installs the vanilla server, Fabric server launcher, Quilt server launcher, or Forge/NeoForge server. Fabric launchers use version-specific filenames, and Forge/NeoForge launch arguments select the exact installed version, so updates cannot select an older loader left on disk. Loader command formats follow the [Fabric server documentation](https://fabricmc.net/use/?page=server), [Quilt server documentation](https://quiltmc.org/en/install/server/), and [Forge installer](https://github.com/MinecraftForge/Installer/blob/2.0/src/main/java/net/minecraftforge/installer/SimpleInstaller.java).

Successful setup saves Java, Minecraft and loader versions, pack version, launch arguments and setup notes in `.mclp-server.json`. Write failures are reported as setup failures. Configuration-copy state is saved in `.mclp-server-prepared`. Installer output goes to `minecraft/logs/server-installer.log`.

## Mods

The **Mods** tab lists the server's `mods/` folder (`.jar` and `.jar.disabled` files, sorted by name ignoring case, from `ListServerMods`, which uses the same `ListContent` as the Manage page; see [manage.md](manage.md#content)). It has a filter box and an on/off switch per mod, for example to turn off a client-side mod that crashes the server. Switching renames `<name>.jar` to or from `<name>.jar.disabled` (`SetServerModEnabled`). If both copies exist, the mod counts as on, and switching keeps only the copy being switched to. Switches are disabled while the server runs or is being set up, and changes apply the next time it starts. The list is cached per server and re-read after setup and when **Refresh** is pressed. When the server has no mods, the tab says so.

## Docker

The **Docker** tab can switch a server to [docker-minecraft-server](https://github.com/itzg/docker-minecraft-server) instead of a Windows process.

- **Enable Containerized Server (docker-minecraft-server)** is a per-server switch. It is saved as the marker file `.mclp-docker` in the server folder (`ServerUsesDocker` / `SetServerUsesDocker`) and is disabled while the server runs.
  - When it is on, the Run tab's **Start in Docker** and **Stop in Docker** replace **Start server**.
  - When it is off, the server runs with Java and the launcher shows its console.
  - The container uses the same folder, world and settings either way.

- **Write compose file** writes `servers/<pack id>/docker-compose.yml` (`WriteDockerCompose` / `DockerComposeText`). It uses the image `itzg/minecraft-server:<tag>`, a container named `mclp-<pack id>` (lowercased, other characters replaced with `-`), the server port published as `<port>:<port>`, and the folder mounted as `/data`. Environment: `TYPE` (`VANILLA`, `FABRIC`, `QUILT`, `FORGE` or `NEOFORGE`) with `VERSION` and the loader's version variable (`FABRIC_LOADER_VERSION`, `QUILT_LOADER_VERSION`, `FORGE_VERSION`, `NEOFORGE_VERSION`), `MEMORY` from the memory slider, `EULA` (`TRUE` only after the player agreed on the page), and `SKIP_SERVER_PROPERTIES` so the image keeps the launcher's `server.properties`. Comments at the top list the `docker compose` commands.
- The image tag (`DockerImageTag`) follows the Java version the Minecraft version asks for, saved as `javaMajor` in `.mclp-server.json` during setup: `java8`, `java11`, `java17`, `java21` or `java25`. Servers set up before this fall back to Minecraft-version rules (1.16 and older use Java 8, up to 1.20.4 use 17, then 21, and 26.1+ use 25).
- A Host IP can't be used inside a container. When one is set, writing and starting are refused, and the tab says "Docker needs Host IP set to "All network adapters"".
- Each time the page opens, a worker runs `docker version`, which shows whether Docker is installed and its engine is running.
- With Docker ready, **Start in Docker** (`docker compose up -d`) and **Stop in Docker** (`docker compose down`) rewrite the compose file and run on a worker, with output in `logs/docker.log`. Start needs the EULA.
- Without Docker, the tab links to Docker's install page.
- With the engine stopped, it says "Docker isn't running. Start Docker Desktop." with **Check again** below. These notes are one short red paragraph, left-aligned and wrapped, shown on both the Run and Docker tabs.
- The compose file can always be written for use elsewhere.
- After a successful **Start in Docker** the header says "Running in Docker", until **Stop in Docker**. The launcher doesn't otherwise track the container's state or console. Use `docker compose logs -f` in the server folder.

## Deleting a server

**Delete...** in the Server files card (or **Delete server...** when setup never finished) asks for confirmation, then deletes `servers/<pack id>/` on a worker with `RemoveServer`, using `\\?\` paths so deeply nested files go too. It is disabled while the server runs or is being set up. The pack stays in My Library.

Uninstalling a pack from My Library can delete its server too. A server whose pack was uninstalled stays on the Servers page, labelled "Pack uninstalled" and named after its folder. It can still be configured, started and deleted, but not updated. The page rescans `servers/` for these each time it opens.

## Settings and running

- Memory is stored per server in `.mclp-memory` and supplied as Java's `-Xmx` option.
- **Host IP** sets `server-ip`, the address the server listens on. It offers **All network adapters** (empty, the default), every IPv4 address on an adapter that is up (`Platform::LocalNetworkAddresses`, which skips loopback and 169.254.x.x addresses, and includes VPN adapters such as Tailscale or ZeroTier), and **Custom address...**. A custom address must be empty, a plain IPv4 address or `localhost` (`IsValidServerIp`). `localhost` and `127.x.x.x` (`IsLoopbackServerIp`) don't trigger the adapter warning; the page notes that only players on this PC can join. It is saved only when valid. The page warns when no adapter on the PC has the address, because the server can't start then. The address list is re-read each time the page opens.
- The page edits message of the day, port, host IP, max players, game mode, difficulty, view distance, PvP, whitelist and online mode in `server.properties`. Unknown properties and comments are kept. Changes apply on restart.
- The player must explicitly accept the linked Minecraft EULA before starting. Setup never accepts it. Consent writes `eula=true` to `eula.txt`.
- **Start server** launches without a separate console window. **Stop server** sends `stop` so Minecraft saves the world. **Force stop** terminates the process.
- The page shows localhost and the detected LAN IPv4 address, with a Copy button for the LAN address, and identifies the port used for internet connections. When a host IP is set, both address lines show it instead. A loopback host IP (`localhost` or `127.x.x.x`) shows only the line for this PC. In place of the port-forwarding note, it says to change Host IP to let friends in.
- The console is a `LogView` (see [manage.md](manage.md)). It highlights warnings and errors and follows new lines when scrolled to the bottom. Text can be selected with the mouse and copied with Ctrl+C or the right-click menu. It accepts server commands. The launcher copies lines from the process only when `ServerProcess::TotalLines` changes. Starting the server clears the console and its selection. Output is also appended to `servers/<pack id>/logs/launcher-console.log`.
- Closing the launcher asks running servers to save and stop, waits up to 20 seconds per server, then terminates any that remain. Setup workers are cancelled and joined.

`ServerProcess` in `src/Util/Process.{h,cpp}` owns the child process, stdin/stdout pipes and reader thread. Console lines are guarded by a mutex and bounded to the most recent 2,000 entries. Child processes (servers, Minecraft, installers) inherit only their own standard handles (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), so one process never holds another's console pipe or log open. Destroying a `ServerProcess` cancels its reader's blocking read (`CancelSynchronousIo`) if a process the server started still holds the pipe.

# Dedicated servers

The **Servers** page hosts installed Modrinth packs on Windows. My Library's **Server** button opens the pack on that page. Packs need an extracted `modrinth.index.json`; whole-pack installs from other sources are listed but cannot be hosted yet.

## Setup (`src/Game/ServerSetup.{h,cpp}`)

**Create server files** runs on a background worker and reports progress, errors and a Cancel action. Files go in `servers/<pack id>/`, separate from the singleplayer instance. The setup reads the pack index with the server environment filter, omitting files marked `server: unsupported`. It copies common overrides and server overrides, with server overrides taking precedence; client overrides are excluded. Existing server configuration and worlds are preserved when **Update server files** runs. The marker `.mclp-server-prepared` records the pack version and every file it listed; when a different version is set up, files the old version listed and the new one doesn't (dropped mods) are deleted first.

**Pack version:** above the buttons, a dropdown (the same version list as Discover, see [discover.md](discover.md)) chooses which version of the pack the server gets. It starts on the pack's active version in My Library, shown as "<version> (installed)", and is disabled while the server runs. Once the server is set up, "Server has <version>" shows what it actually runs. Picking another version (`ServerState::version`) makes **Create/Update server files** download that version's `.mrpack` into `downloads/<pack id>/server/` and unpack it into `servers/<pack id>/.mclp-pack/` (emptied first), then set up from there instead of the active version's install folder. Picking another version that's installed in My Library uses that version's install folder instead of downloading it. Modrinth packs installed before versions had ids are matched by version number.

Java comes from Settings > Minecraft or Mojang's runtime for the Minecraft version. Setup installs the vanilla server, Fabric server launcher, Quilt server launcher, or Forge/NeoForge server. Fabric launchers use version-specific filenames, and Forge/NeoForge launch arguments select the exact installed version, so updates cannot select an older loader left on disk. Loader command formats follow the [Fabric server documentation](https://fabricmc.net/use/?page=server), [Quilt server documentation](https://quiltmc.org/en/install/server/), and [Forge installer](https://github.com/MinecraftForge/Installer/blob/2.0/src/main/java/net/minecraftforge/installer/SimpleInstaller.java).

Successful setup saves Java, Minecraft and loader versions, pack version and launch arguments in `.mclp-server.json`. Write failures are reported as setup failures. Configuration-copy state is saved in `.mclp-server-prepared`. Installer output goes to `minecraft/logs/server-installer.log`.

## Settings and running

- Memory is stored per server in `.mclp-memory` and supplied as Java's `-Xmx` option.
- The page edits message of the day, port, max players, game mode, difficulty, view distance, PvP, whitelist and online mode in `server.properties`. Unknown properties and comments are kept. Changes apply on restart.
- The player must explicitly accept the linked Minecraft EULA before starting. Setup never accepts it. Consent writes `eula=true` to `eula.txt`.
- **Start server** launches without a separate console window. **Stop server** sends `stop` so Minecraft saves the world. **Force stop** terminates the process.
- The page shows localhost and the detected LAN IPv4 address, with a Copy button for the LAN address. It also identifies the port used for internet connections.
- The console shows recent output, highlights warnings/errors, follows new lines when at the bottom, and accepts server commands. Output is also appended to `servers/<pack id>/logs/launcher-console.log`.
- Closing the launcher asks running servers to save and stop, waits up to 20 seconds per server, then terminates any that remain. Setup workers are cancelled and joined.

`ServerProcess` in `src/Util/Process.{h,cpp}` owns the child process, stdin/stdout pipes and reader thread. Console lines are guarded by a mutex and bounded to the most recent 2,000 entries.

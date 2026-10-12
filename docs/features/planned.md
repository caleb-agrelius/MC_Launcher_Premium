# Planned / Not Yet Implemented

Features the UI mentions that don't exist yet:

- **Launching Minecraft:** the PLAY button on Discover is disabled and labeled "COMING SOON".
- **Minecraft installation:** downloading or managing the game itself, Java runtimes, and mod loaders.
- **Account sign-in:** Microsoft authentication (needs an Azure app client ID approved for the Minecraft API). Settings > Account shows a placeholder with a disabled **Sign in with Microsoft** button.
- **Using the launch settings:** Settings > Minecraft saves memory, Java, JVM argument, window, and after-launch defaults, but nothing reads them until launching exists (see [settings.md](settings.md)).
- **Full `.mrpack` installs:** extraction unpacks the manifest and `overrides/`, but doesn't yet download the mod jars listed in `modrinth.index.json` or merge `overrides/` into a game directory.

Unused scaffolding: `src/App.h` declares `RunApp()`, which has no definition and no callers.

When one of these features is built, move it into the matching feature doc (or add a new one) and remove it here.

# Planned / Not Yet Implemented

Features the UI mentions that don't exist yet:

- **Launching Minecraft:** the PLAY button on Discover is disabled and labeled "COMING SOON".
- **Minecraft installation:** downloading or managing the game itself, Java runtimes, and mod loaders.
- **Account sign-in:** Microsoft authentication (needs an Azure app client ID approved for the Minecraft API).
- **Modpack source follow-ups:** a picker for ATLauncher optional mods, loader detection for single-zip Technic packs, and a live CurseForge test with a real API key. The original spec is [plans/modpack-sources.md](../plans/modpack-sources.md).
- **Full `.mrpack` installs:** extraction unpacks the manifest and `overrides/`, but doesn't yet download the mod jars listed in `modrinth.index.json` or merge `overrides/` into a game directory.

Unused scaffolding: `src/App.h` declares `RunApp()`, which has no definition and no callers.

When one of these features is built, move it into the matching feature doc (or add a new one) and remove it here.

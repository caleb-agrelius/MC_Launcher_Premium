# Planned / Not Yet Implemented

Features that don't exist yet:

- **Microsoft sign-in approval:** the sign-in flow is built (see [accounts.md](accounts.md)), but it needs an Azure app client ID that Mojang has approved for the Minecraft API. Until one is set (Settings > Account, saved as `microsoftClientId`), only offline mode works.
- **Updating packs in place:** My Library can repair and uninstall a pack, but not move it to a newer version. Installing the new version from Discover (**Reinstall** for whole-pack sources) is the way for now.
- **Servers for other sources:** one-click servers read an `.mrpack`'s `modrinth.index.json`, which marks client-only mods. CurseForge, FTB, Technic and ATLauncher manifests don't reliably say which mods are client-only, so their packs can't be hosted yet.
- **Discover paging:** each search shows one page of results (`searchLimit`).
- **ATLauncher jar mods:** `jar`/`forge` mods are saved to `jarmods/` but not patched into the game jar, so very old packs that need them may not start.
- **Other platforms:** launching is Windows x64 only.
- **Modpack source follow-ups:** a live CurseForge test with a real API key. The original spec is [plans/modpack-sources.md](../plans/modpack-sources.md).

When one of these features is built, move it into the matching feature doc (or add a new one) and remove it here.

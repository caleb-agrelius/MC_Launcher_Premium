# Planned / Not Yet Implemented

Features that don't exist yet:

- **Microsoft sign-in approval:** the sign-in flow is built (see [accounts.md](accounts.md)), but it needs an Azure app client ID that Mojang has approved for the Minecraft API. Until one is set (Settings > Account, saved as `microsoftClientId`), only offline mode works.
- **Multiple accounts:** only one account is saved at a time.
- **Instance management:** no buttons to view logs, repair, update or uninstall a pack. (My Library can open the instance folder.)
- **Per-pack launch settings:** Settings > Minecraft applies to every pack; there are no per-pack overrides.
- **Other platforms:** launching is Windows x64 only.

Unused scaffolding: `src/App.h` declares `RunApp()`, which has no definition and no callers.

When one of these features is built, move it into the matching feature doc (or add a new one) and remove it here.

# Planned / Not Yet Implemented

Features that don't exist yet:

- **Microsoft sign-in approval:** the sign-in flow is built (see [accounts.md](accounts.md)), but it needs an Azure app client ID that Mojang has approved for the Minecraft API. Until one is set in `settings.json`, only offline mode works.
- **Multiple accounts:** only one account is saved at a time.
- **Settings UI for memory:** `maxMemoryMb` can only be changed in `settings.json`.
- **Instance management:** no buttons to open the instance folder, view logs, repair, update or uninstall a pack.
- **Other platforms:** launching is Windows x64 only.

Unused scaffolding: `src/App.h` declares `RunApp()`, which has no definition and no callers.

When one of these features is built, move it into the matching feature doc (or add a new one) and remove it here.

# Pack Manage Page

A full page for one My Library pack, opened with **Manage** on its Library card. UI code is in `src/UI/Pages/Manage/`: `ManagePage` is the coordinator (tab strip, reloads) and each tab is its own `IManageTab` (`OverviewTab` with `OverviewCards`, `ContentTab` over a shared `ContentModel` with `ContentBrowser`, `ChangelogTab`, `LogsTab` with `LogReader`, `ScreenshotsTab`), under `ManageHeader`. Tabs ask for actions through `ManageFrame`/`ManageRequest` and never touch each other. `ManagePageHost` (`src/UI/Pages/Manage/`) shows it as `Page::Manage` for the selected pack, passes a `ManagePageContext` each frame (the pack, its active version's game and install folders, the image cache, play state, and functions to run work on a worker and post results back), and carries out the `ManageRequest` the page returns (play, cancel, stop, launch settings, repair, server, uninstall, make a version active, back). The sidebar keeps **My Library** selected. The page closes back to My Library if the pack is uninstalled.

## Header
- **< My Library** goes back. Next to it: source, version, Minecraft version and loaders, and the **Modified** chip when the player changed the active version (see [Your changes](#your-changes)).
- **Open folder** opens the active version's game folder. **PLAY** starts the pack; while it's being prepared the button is **Cancel**, and while Minecraft runs it's **Stop game** (with the usual confirmation). The play step, a progress bar and the last error show under the header.
- Tabs (`Widgets::TabStrip`): **Overview**, **Content (n)**, **Changelog**, **Logs (n)** and **Screenshots (n)**. Lists are read when the page opens for a pack or version (`Reload`), and again with each tab's **Refresh**.

## Overview
- The pack's banner, then chips with the number of mods, resource packs, shader packs, worlds (folders in `saves/`) and screenshots, and when it was last played (the time of `logs/latest.log`). Then the pack's summary.
- **Installed versions:** every version with its Minecraft version and loaders. The active one has an **Active** chip, and changed ones a **Modified** chip; the others have **Make active**.
- **Your changes** (only when the active version is modified): see [Your changes](#your-changes). Each has **Uninstall...** (the whole pack when it's the only version). Disabled while the pack is being played or prepared.
- **Manage:** the game folder and pack files folder as read-only, selectable paths with **Open**, then **Launch settings...** (the per-pack dialog, labeled "(custom)" when the pack has its own), **Update...** (`ManageAction::Update`; not for the local index or packs the player made), **Repair files** (not for packs the player made), **Host a server** (when the pack can be hosted; opens it on Servers) and **Uninstall...**. These are the actions My Library's old Manage menu had; see [library.md](library.md) for what they do, and [Updating a pack](library.md#updating-a-pack) for **Update...**. While an update runs, the header shows its progress and PLAY waits for it.

## Content
- Sub-tabs for **Mods**, **Resource packs** and **Shader packs**, each from its folder in the game folder (`mods/`, `resourcepacks/`, `shaderpacks/`). A filter box matches names and file names; **Browse Modrinth**, **Add files...**, **Open folder** and **Refresh** sit beside it.
- **Adding content:** **Add files...** opens the system Open dialog (`Platform::PickFiles`, filtered to `.jar` for mods and `.zip` for packs, several at once). Files and folders dropped on the window while the Manage page is open are added too.
  - `ContentKindForFile` sorts them: `.jar` files are mods, `.zip` files and folders go to the shown tab when it's resource or shader packs, else resource packs. Anything else is refused with a message.
  - `AddContent` copies each into its folder and refuses a name that's already there, on or off.
  - The page switches to the tab the first file went to, says "Added <file>." and re-reads the folders and mod names. Adding is refused while the game is running or being prepared.
- Each row has the name, and a line with the file name, size (or "folder") and author. Files the player added have an **Added** chip; files they switched have a **Changed** chip. Mods show the name, version, loader chip, authors and a shortened description from the jar itself (see below). Disabled entries are dimmed. The on/off switch (left) and **Remove** (right) are centered on the row's height, and descriptions wrap before Remove.
- The switch renames the file or folder to and from `<name>.disabled`, so Minecraft skips it. **Remove** asks inline (**Delete file** / **Keep**) and deletes the entry and any disabled copy. Both are disabled while the game is running or being prepared. Errors (a locked file) show above the list.
- Folder logic is in `src/Game/InstanceContent.{h,cpp}`, shared with the Servers page's mod list:
  - `ListContent(folder, extensions, folders)` lists `.jar` mods, or `.zip` and unzipped folders for resource and shader packs, with their disabled copies, sorted by name ignoring case. A name with both copies counts as enabled.
  - `SetContentEnabled` and `DeleteContent` refuse names that aren't one plain file name.
  - `ReadModMetadata(jar)` reads `fabric.mod.json`, `quilt.mod.json`, `META-INF/neoforge.mods.toml` or `META-INF/mods.toml` (the first `[[mods]]` table; a `${file.jarVersion}` version comes from the manifest's `Implementation-Version`), or old Forge's `mcmod.info`. Jars are read straight from disk with a wide path. The page reads every mod's metadata on a worker when it opens ("Reading mod names...") and ignores results for a pack it no longer shows.

## Browse Modrinth
**Browse Modrinth** replaces the content list with a Modrinth search for the shown kind. Mods, resource packs and shader packs use Modrinth project types `mod`, `resourcepack` and `shader`. **Back to installed** returns to the list. The code is in `src/Network/ModSearch.{h,cpp}`, and the page's part is in `ManagePage::DrawBrowse`.
- **Search** (`SearchContent`): results are limited to the pack's Minecraft version and, for mods, its loader (the first of the pack's loaders). Quilt packs also see Fabric mods.
  - The search runs 0.4 seconds after typing stops. An empty search lists the most downloaded.
  - The line under the box says what's shown, e.g. "Showing mods for Minecraft 26.2 on fabric."
  - Each result has its icon, name, a **Page** link, its author and downloads, and its summary.
- **Installed:** projects the pack already has show "Installed" instead of **Install**. `FindInstalledProjects` looks up the SHA-1 of every file in the three folders with Modrinth's `version_files`, on a worker when the browser opens, so files added by hand or by the pack count too.
- **Install** (`InstallContent`, on a worker) does the following:
  - picks the newest release of the project for the pack's Minecraft version and loader (else its newest version) and downloads its primary file into the kind's folder as `<name>.part`, checks the SHA-1, then renames it
  - adds the mods it requires the same way, and theirs, skipping the ones the pack already has and stopping after 32 files
  - says "Added <name> and N mods it needs.", or lists the required mods that have no fitting version
  - shows "No version for Minecraft <version> with <loader>." on the row when the project itself doesn't fit
  - records each added file as a change for packs from a source (see [Your changes](#your-changes)), then re-reads the folders
  - is disabled while the game is running or being prepared
- New packs open here (see [library.md](library.md#new-packs)).

## Your changes
Adding a file, removing one, or switching one on or off records a change on the active installed version. `InstalledVersion::contentChanges` maps a path in the game folder ("mods/sodium.jar") to `added`, `removed`, `disabled` or `enabled`, and is saved in `library.json` right away (`RecordContentChange`, `src/Game/InstanceContent.{h,cpp}`).
- **Canceling out:** removing an added file, turning a file back, or re-adding a removed name clears the entry, so the version is unmodified again.
- **Modified:** a version with changes shows as modified everywhere it appears:
  - **Manage:** a **Modified** chip in the header and the versions list.
  - **My Library:** a chip on the card, and "modified" in the version dropdown.
  - **Discover:** a chip beside "Installed".
  - **Servers:** "modified pack" in the list and "(installed, modified)" as the pack version.
  - The chip's tooltip lists the counts (`DescribeContentChanges`, e.g. "1 added, 1 turned off").
- **Keeping changes:** PLAY, **Repair files** and reinstalling the version keep the changes. **Update...** installs a new version, which starts without them (the player's resource and shader packs are copied over with their worlds). See [launching.md](launching.md) and [pack-sources.md](pack-sources.md).
  - Removed pack files aren't downloaded again, and files with a `.disabled` copy aren't either.
  - After the pack's files are in place, `ApplyContentChanges` deletes removed files again and switches files back, in case a pack update or override brought them back.
  - Reinstalling the same version (`PlanInstalledVersion`, `AddInstalledVersion`) keeps the version's changes.
- **Servers:** a server built from the version also gets the mods the player added, and doesn't get the pack files they removed (see [servers.md](servers.md)).
- **Your changes card:** on Overview, a card lists every change and offers **Undo my changes...**, which asks first ("Undo changes" / "Keep").
  - `UndoContentChanges` deletes added files and switches changed files back, then the changes are cleared and saved.
  - When files had been removed, **Repair files** runs to download them again; otherwise a toast says the pack is back to its own files.
  - Undo is disabled while the game is running or being prepared.

## Changelog
- Shows "What's new in <version>" with the active version's changelog as plain text (`FetchPackChangelog`, see [pack-sources.md](pack-sources.md#changelogs)), loaded once per pack version on a worker, with **Retry** on failure. A link opens the pack's page for older versions.
- Technic and ATLauncher don't publish changelogs, so the tab says so and links to the pack's page. A pack the player made says it has no changelog.

## Logs
- A list of `logs/latest.log` first, then the other `.log` and `.log.gz` files in `logs/` and the `.txt` files in `crash-reports/`, newest first. Long names end in "...". Drag the gap beside the list to make it wider or narrower (kept while the launcher runs).
- The chosen log is read on a worker (gzip logs are unpacked with miniz; logs over 24 MB show their last 24 MB) and shown in a `LogView` (see [ui-theme.md](ui-theme.md#log-view)), scrolled to the end. ERROR and WARN lines are tinted. Text can be selected and copied.
- **Show only lines containing...** filters the lines, ignoring case. **Open file** and **Open folder** open the log in its default program or its folder.

## Screenshots
- A grid of `screenshots/` PNG and JPEG files, newest first, as 16:9 thumbnails with their file names. Thumbnails load through `ImageCache` as 480 px images (`ImageCache::FileKey(path, 480)`), so they stay small in memory.
- Clicking one opens a viewer: the full image fitted to the window, its name and position ("2 of 3"), **<** and **>** (or the arrow keys), **Open** and **Show in folder**. Escape or the close button closes it.

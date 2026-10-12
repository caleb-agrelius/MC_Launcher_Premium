# Shared Mod Files

Mods and resource and shader packs that several packs use are kept once on disk. Code: `src/Util/SharedFiles.{h,cpp}`. The setting and its controls are on Settings > Downloads (see [settings.md](settings.md)).

## How it works
- **Store:** `minecraft/shared-files/<first 2 hex digits>/<key>`, one file per distinct content. The key is `sha1-<hex>` when the file's SHA-1 is known, else `md5-<hex>` (ATLauncher only gives MD5s).
- **Hard links:** packs get a hard link to the stored file instead of their own copy. Every pack still sees an ordinary file at its usual path, so Minecraft, the Manage page and repairs work unchanged.
- **What's shared:** only files under a game folder's `mods/`, `resourcepacks/` and `shaderpacks/` (`IsShareablePath`), and only when their hash is known up front. Configs, saves and options are never shared, because the game writes to them.

## When files are shared
- **Installing:** every path that downloads a pack's files tries the store first.
  - `DownloadAll` jobs with `share` set: `.mrpack` files from `ReadMrpackIndex`, used both for Play and for server setup.
  - Install plans (`RunInstallPlan`) for CurseForge, FTB, Technic and ATLauncher.
  - Manage > Content > **Browse Modrinth** installs (`ModSearch`).
- **Linking from the store (`LinkTo`):** the stored copy must have the expected size, and its contents are hash-checked before linking.
  - A copy whose contents don't match its hash is deleted from the store and the file is downloaded instead. A damaged copy is never handed to another pack.
  - A size that differs from what the caller expects just means "download it". The stored copy is kept for others.
- **Adding to the store (`Add`):** a fresh, verified download is added as another link. Files an install plan finds already in place and verified are added too.
- **Sharing what packs already have:** Settings > Downloads > **Share files packs already have** (`ShareExisting`) runs on a worker.
  - It hashes every `.jar`, `.zip` and `.disabled` file in the mods, resource pack and shader pack folders of every installed version and every server folder.
  - The first copy of each file goes into the store. Later copies are swapped for a link: the link is made under a temporary name first and then renamed over the copy, so the file is never missing.
  - It's disabled while any game is running, because the game's mods are open.
  - It reports how many files were checked and shared, and the space freed.
- **Statistics (`StoreStats`):** distinct files, their size counted once, how many are used by two or more packs, and the bytes those extra links save. They're shown on the Settings card, counted on a worker.

## Removing
- Deleting, renaming (turning a mod off as `.disabled`) or replacing a pack's file only affects that pack: it removes or moves that one link.
- `Prune` deletes stored files whose only remaining link is the store's own. It runs at startup and after every uninstall. The startup log notes the bytes freed.

## Turning it off
Settings > Downloads > **Share identical files** (`shareModFiles` in `settings.json`, on by default). Off: `LinkTo` and `Add` do nothing, so new installs get their own copies. Files already shared stay shared.

## Things to know
- **Same drive only:** hard links need the store and the packs on the same NTFS drive. Where linking fails, files are downloaded as separate copies and **Share files packs already have** reports the error.
- **Explorer sizes:** Explorer counts every link at full size, so folder sizes don't show the saving. The Settings card shows the real figure.
- **Edits in place:** writing *into* a shared file would change it in every pack that shares it. Minecraft and the launcher never do that: mods are only read, and updates replace files.
- **Repairs:** a damaged shared file is damaged in every pack that links it. **Repair files** for one pack deletes its link and downloads a fresh copy, which then becomes the stored one. Other packs keep the damaged file until they're repaired too.

## Verification
`launcher_checks` (`SharedFileChecks`) covers:
- keys and shareable paths;
- sharing two existing copies into one file, with configs left alone, and the stats;
- linking a new install, and refusing a size mismatch;
- turning a mod off in one pack without affecting the others;
- dropping a damaged stored copy instead of linking it;
- sharing turned off;
- pruning.

In a live run, Fabulously Optimized 13.6.0 and 14.1.1 installed side by side shared the 7 mods the two versions have in common.

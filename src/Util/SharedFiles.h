#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Identical files (mods, resource and shader packs) shared between packs to save drive
// space. Each distinct file is kept once in a store folder, named by its hash, and packs
// get a hard link to it: one copy on disk, while every pack still sees an ordinary file.
//   - Only files with a known SHA-1 or MD5 are shared, and a stored copy is hash-checked
//     before it's linked, so a damaged copy isn't spread to another pack.
//   - Removing, renaming (.disabled) or replacing a pack's file only affects that pack.
//     Writing into a shared file in place would change it everywhere, but Minecraft and
//     the launcher never do that.
//   - Hard links need the store and the packs on the same NTFS drive. Where linking
//     fails, files are simply downloaded and kept as separate copies.
//   - Explorer counts every link at full size, so folder sizes don't show the saving.
namespace SharedFiles {

// Whether a file at this path in a game folder may be shared: only content the game reads
// but never writes (mods/, resourcepacks/, shaderpacks/), never configs or saves.
bool IsShareablePath(const std::filesystem::path& relativePath);

// Sets the store folder and turns sharing on or off (Settings > Downloads). Off: LinkTo
// and Add do nothing; files already linked stay as they are.
void Configure(const std::filesystem::path& storeDir, bool enabled);
bool Enabled();
std::filesystem::path StoreDir();

// The store key for a file with these hashes: "sha1-<hex>", else "md5-<hex>", else "".
std::string KeyFor(const std::string& sha1, const std::string& md5 = {});

// Puts the stored copy of key at target (replacing any file there) as a hard link. The
// stored copy must have the expected size (when nonzero) and hash; a copy whose contents
// don't match its hash is deleted from the store. False when there's no usable copy or
// linking failed, so the caller downloads the file as usual.
bool LinkTo(const std::string& key, std::uint64_t size, const std::filesystem::path& target);

// Records a downloaded, already verified file in the store (as another hard link to it),
// so later installs can share it. Does nothing if the store has it or linking fails.
void Add(const std::string& key, const std::filesystem::path& file);

// Deletes stored files that no pack uses any more (their only link is the store's).
// Returns the bytes freed.
std::uint64_t Prune();

struct Stats {
	std::size_t files = 0;    // distinct files in the store
	std::uint64_t bytes = 0;  // their size on disk, counted once
	std::size_t shared = 0;   // of those, files used by two or more packs
	std::uint64_t saved = 0;  // bytes saved by those extra links
};
Stats StoreStats();

struct ShareResult {
	std::size_t scanned = 0;    // files looked at
	std::size_t linked = 0;     // files replaced by a link to a stored copy
	std::uint64_t saved = 0;    // bytes freed by that
	std::string error;          // first problem, if any (the rest still ran)
};
// Shares files packs already have: hashes every .jar, .zip and .disabled file in the
// mods/, resourcepacks/ and shaderpacks/ folders of each game folder, and replaces copies
// of a file the store already has with links to it (adding the first copy of each to the
// store). progress gets (done, total).
ShareResult ShareExisting(const std::vector<std::filesystem::path>& gameDirs,
						  const std::function<void(std::size_t, std::size_t)>& progress,
						  const std::atomic<bool>* cancel);

}

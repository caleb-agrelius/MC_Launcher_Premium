#include "SharedFiles.h"

#include "Util/Hash.h"
#include "Util/Paths.h"
#include "Util/Sha1.h"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace fs = std::filesystem;

namespace SharedFiles {

namespace {

std::mutex g_mutex; // store changes (several download threads add and link at once)
fs::path g_storeDir;
std::atomic<bool> g_enabled{ false };

bool IsHex(const std::string& text)
{
	return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string Lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

// store/<first two hex digits>/<key>, e.g. store/ab/sha1-ab12...
fs::path PathFor(const std::string& key)
{
	const std::size_t dash = key.find('-');
	const std::string hex = dash == std::string::npos ? key : key.substr(dash + 1);
	return g_storeDir / hex.substr(0, 2) / key;
}

// Whether the file's contents match the key's hash.
bool Matches(const std::string& key, const fs::path& file)
{
	if (key.rfind("sha1-", 0) == 0)
		return Sha1File(file.string()) == key.substr(5);
	if (key.rfind("md5-", 0) == 0)
		return Md5HexOfFile(LongPath(file).string()) == key.substr(4);
	return false;
}

bool StoreUsable()
{
	return g_enabled.load() && !g_storeDir.empty();
}

}

bool IsShareablePath(const fs::path& relativePath)
{
	const fs::path normal = relativePath.lexically_normal();
	if (normal.empty() || normal.is_absolute())
		return false;
	const std::string top = Lower(normal.begin()->string());
	return top == "mods" || top == "resourcepacks" || top == "shaderpacks";
}

void Configure(const fs::path& storeDir, bool enabled)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_storeDir = storeDir;
	g_enabled = enabled;
}

bool Enabled()
{
	return g_enabled.load();
}

fs::path StoreDir()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_storeDir;
}

std::string KeyFor(const std::string& sha1, const std::string& md5)
{
	const std::string s = Lower(sha1);
	if (s.size() == 40 && IsHex(s))
		return "sha1-" + s;
	const std::string m = Lower(md5);
	if (m.size() == 32 && IsHex(m))
		return "md5-" + m;
	return {};
}

bool LinkTo(const std::string& key, std::uint64_t size, const fs::path& target)
{
	if (key.empty() || !StoreUsable())
		return false;
	std::lock_guard<std::mutex> lock(g_mutex);
	const fs::path stored = PathFor(key);
	std::error_code ec;
	if (!fs::is_regular_file(LongPath(stored), ec))
		return false;
	// A different size than the caller expects: not the file it wants (or its metadata is
	// off), so download instead; the stored copy may still be right for others.
	if (size != 0 && fs::file_size(LongPath(stored), ec) != size)
		return false;
	if (!Matches(key, stored)) {
		fs::remove(LongPath(stored), ec); // damaged: never hand it out again
		return false;
	}
	fs::create_directories(LongPath(target.parent_path()), ec);
	fs::remove(LongPath(target), ec);
	ec.clear();
	fs::create_hard_link(LongPath(stored), LongPath(target), ec);
	return !ec;
}

void Add(const std::string& key, const fs::path& file)
{
	if (key.empty() || !StoreUsable())
		return;
	std::lock_guard<std::mutex> lock(g_mutex);
	const fs::path stored = PathFor(key);
	std::error_code ec;
	if (fs::exists(LongPath(stored), ec))
		return;
	fs::create_directories(LongPath(stored.parent_path()), ec);
	fs::create_hard_link(LongPath(file), LongPath(stored), ec); // fails across drives: then it isn't shared
}

std::uint64_t Prune()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	std::uint64_t freed = 0;
	std::error_code ec;
	if (g_storeDir.empty() || !fs::is_directory(g_storeDir, ec))
		return 0;
	std::vector<fs::path> unused;
	for (auto it = fs::recursive_directory_iterator(g_storeDir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		std::error_code fileError;
		if (it->is_regular_file(fileError) && fs::hard_link_count(it->path(), fileError) <= 1 && !fileError)
			unused.push_back(it->path());
	}
	for (const fs::path& path : unused) {
		std::error_code fileError;
		const std::uint64_t size = fs::file_size(path, fileError);
		if (fs::remove(path, fileError))
			freed += size;
	}
	return freed;
}

Stats StoreStats()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	Stats stats;
	std::error_code ec;
	if (g_storeDir.empty() || !fs::is_directory(g_storeDir, ec))
		return stats;
	for (auto it = fs::recursive_directory_iterator(g_storeDir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		std::error_code fileError;
		if (!it->is_regular_file(fileError))
			continue;
		const std::uint64_t size = fs::file_size(it->path(), fileError);
		const std::uintmax_t links = fs::hard_link_count(it->path(), fileError);
		++stats.files;
		stats.bytes += size;
		// The store holds one link; each pack another. Copies beyond the first are the saving.
		if (links >= 3) {
			++stats.shared;
			stats.saved += size * (links - 2);
		}
	}
	return stats;
}

ShareResult ShareExisting(const std::vector<fs::path>& gameDirs, const std::function<void(std::size_t, std::size_t)>& progress,
	const std::atomic<bool>* cancel)
{
	ShareResult result;
	if (!StoreUsable()) {
		result.error = "Sharing is turned off";
		return result;
	}
	std::vector<fs::path> files;
	for (const fs::path& gameDir : gameDirs) {
		for (const char* folder : { "mods", "resourcepacks", "shaderpacks" }) {
			std::error_code ec;
			const fs::path dir = gameDir / folder;
			if (!fs::is_directory(LongPath(dir), ec))
				continue;
			for (auto it = fs::recursive_directory_iterator(ExtendedPath(dir), ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
				std::error_code fileError;
				const std::string ext = Lower(it->path().extension().string());
				if (it->is_regular_file(fileError) && (ext == ".jar" || ext == ".zip" || ext == ".disabled"))
					files.push_back(it->path());
			}
		}
	}
	for (const fs::path& file : files) {
		if (cancel && cancel->load())
			break;
		if (progress)
			progress(result.scanned, files.size());
		++result.scanned;
		std::error_code ec;
		const std::uint64_t size = fs::file_size(file, ec);
		if (ec || size == 0)
			continue;
		const std::string key = KeyFor(Sha1File(file.string()));
		if (key.empty())
			continue;
		std::lock_guard<std::mutex> lock(g_mutex);
		const fs::path stored = PathFor(key);
		if (!fs::exists(LongPath(stored), ec)) {
			// First copy seen: it becomes the stored one.
			fs::create_directories(LongPath(stored.parent_path()), ec);
			ec.clear();
			fs::create_hard_link(file, LongPath(stored), ec);
			if (ec && result.error.empty())
				result.error = "Couldn't link into the store (" + ec.message() + "); the store must be on the same drive";
			continue;
		}
		if (fs::equivalent(file, LongPath(stored), ec))
			continue; // already shared
		// Swap the copy for a link: link to a temporary name first, so the file is never missing.
		fs::path temp = file;
		temp += ".mclp-link";
		fs::remove(temp, ec);
		ec.clear();
		fs::create_hard_link(LongPath(stored), temp, ec);
		if (ec) {
			if (result.error.empty())
				result.error = "Couldn't link " + file.filename().string() + ": " + ec.message();
			continue;
		}
		fs::rename(temp, file, ec);
		if (ec) {
			std::error_code ignored;
			fs::remove(temp, ignored);
			if (result.error.empty())
				result.error = "Couldn't replace " + file.filename().string() + ": " + ec.message();
			continue;
		}
		++result.linked;
		result.saved += size;
	}
	if (progress)
		progress(result.scanned, files.size());
	return result;
}

}

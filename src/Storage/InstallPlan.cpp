#include "InstallPlan.h"

#include "Network/HttpClient.h"
#include "Storage/Installer.h"
#include "Util/Hash.h"
#include "Util/Paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string ToLower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

// Checks a file on disk against the plan's hash, or its size when there's no hash.
// Returns an empty string when it matches, else a description of the mismatch.
static std::string VerifyFile(const PlannedFile& file, const std::string& path)
{
	if (!file.sha1.empty()) {
		const std::string actual = Sha1HexOfFile(path);
		return actual == ToLower(file.sha1) ? std::string() : "SHA-1 mismatch";
	}
	if (!file.md5.empty()) {
		const std::string actual = Md5HexOfFile(path);
		return actual == ToLower(file.md5) ? std::string() : "MD5 mismatch";
	}
	if (file.size > 0) {
		std::error_code ec;
		const auto size = fs::file_size(path, ec);
		return (!ec && size == file.size) ? std::string() : "size mismatch";
	}
	return {};
}

// A file already on disk counts as installed only if it can be verified.
static bool AlreadyInstalled(const PlannedFile& file, const std::string& path)
{
	if (file.sha1.empty() && file.md5.empty() && file.size == 0)
		return false;
	std::error_code ec;
	return fs::is_regular_file(path, ec) && VerifyFile(file, path).empty();
}

// The files a plan puts in minecraft/ by name (not extracted zips, whose contents
// aren't known up front), saved in pack.json so the next install can remove the ones
// a newer or older pack version no longer has.
static std::vector<std::string> InstalledFiles(const InstallPlan& plan)
{
	std::vector<std::string> files;
	for (const PlannedFile& file : plan.files) {
		if (!file.extract)
			files.push_back(fs::path(fs::u8path(file.relPath)).lexically_normal().generic_string());
	}
	return files;
}

static std::vector<std::string> PreviousFiles(const fs::path& packJson)
{
	std::ifstream in(packJson);
	if (!in.is_open())
		return {};
	try {
		json j;
		in >> j;
		return j.value("files", std::vector<std::string>());
	}
	catch (...) {
		return {};
	}
}

static bool WritePackJson(const InstallPlan& plan, const std::string& path, const std::vector<PlannedFile>& blocked)
{
	json blockedJson = json::array();
	for (const PlannedFile& file : blocked) {
		blockedJson.push_back({
			{ "name", file.name },
			{ "path", file.relPath },
			{ "page", file.manualPageUrl },
			{ "reason", file.blockedReason },
		});
	}
	const json j = {
		{ "source", PackSourceKey(plan.source) },
		{ "sourceId", plan.sourceId },
		{ "sourceVersionId", plan.sourceVersionId },
		{ "version", plan.versionName },
		{ "minecraft", plan.minecraftVersion },
		{ "loader", plan.loader },
		{ "loaderVersion", plan.loaderVersion },
		{ "blockedFiles", blockedJson },
		{ "files", InstalledFiles(plan) },
		{ "notes", plan.notes },
	};
	std::ofstream ofs(path, std::ios::trunc);
	if (!ofs.is_open())
		return false;
	ofs << j.dump(2) << "\n";
	return ofs.good();
}

bool RunInstallPlan(HttpClient& http,
					const InstallPlan& plan,
					const std::string& installDir,
					std::function<void(const InstallProgress&)> progress,
					std::vector<PlannedFile>& blocked,
					std::string* err,
					const std::atomic<bool>* cancel)
{
	blocked.clear();
	const fs::path root(installDir);
	const fs::path gameDir = root / "minecraft";
	const fs::path cacheDir = root / ".cache";

	// Manifests come from the network: reject any path that would escape the instance.
	std::vector<const PlannedFile*> toDownload;
	for (const PlannedFile& file : plan.files) {
		if (!IsSafeRelativePath(file.relPath)) {
			if (err) *err = "Unsafe path in pack manifest: " + file.relPath;
			return false;
		}
		if (file.url.empty())
			blocked.push_back(file);
		else
			toDownload.push_back(&file);
	}

	std::error_code ec;
	fs::create_directories(gameDir, ec);
	fs::create_directories(cacheDir, ec);
	if (ec) {
		if (err) *err = "Could not create " + gameDir.string() + ": " + ec.message();
		return false;
	}

	// Downloads run on a few threads that take the next file from a shared index.
	std::mutex mutex;
	std::size_t next = 0;
	std::uint64_t done = 0;
	std::string firstError;
	std::atomic<bool> failed{ false };
	const std::uint64_t total = toDownload.size();
	auto report = [&](const char* stage, std::uint64_t now, std::uint64_t count) {
		if (progress)
			progress({ stage, now, count });
	};
	report("Downloading files", 0, total);

	auto worker = [&]() {
		for (;;) {
			std::size_t index;
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (next >= toDownload.size() || failed.load() || (cancel && cancel->load()))
					return;
				index = next++;
			}
			const PlannedFile& file = *toDownload[index];
			std::string error;
			if (file.extract) {
				// Download to the cache, verify, unpack into relPath, then delete the zip.
				const std::string zipPath = (cacheDir / ("extract-" + std::to_string(index) + ".zip")).string();
				if (!http.DownloadToFile(file.url, zipPath, nullptr, &error, cancel)) {
					error = "download failed: " + error;
				} else if (const std::string mismatch = VerifyFile(file, zipPath); !mismatch.empty()) {
					error = mismatch;
				} else {
					const std::string target = (gameDir / fs::path(file.relPath).lexically_normal()).string();
					if (!ExtractZip(zipPath, target, file.extractPrefix, nullptr, &error, cancel))
						error = "extract failed: " + error;
				}
				std::error_code removeError;
				fs::remove(zipPath, removeError);
			} else {
				const fs::path dest = LongPath(gameDir / fs::path(file.relPath).lexically_normal());
				const std::string destPath = dest.string();
				if (!AlreadyInstalled(file, destPath)) {
					std::error_code dirError;
					fs::create_directories(dest.parent_path(), dirError);
					const std::string partPath = destPath + ".part";
					if (!http.DownloadToFile(file.url, partPath, nullptr, &error, cancel)) {
						error = "download failed: " + error;
					} else if (const std::string mismatch = VerifyFile(file, partPath); !mismatch.empty()) {
						error = mismatch;
						fs::remove(partPath, dirError);
					} else {
						fs::remove(dest, dirError);
						fs::rename(partPath, dest, dirError);
						if (dirError)
							error = "could not save: " + dirError.message();
					}
				}
			}

			std::lock_guard<std::mutex> lock(mutex);
			if (!error.empty()) {
				if (!failed.exchange(true))
					firstError = file.name + ": " + error;
				return;
			}
			++done;
			report("Downloading files", done, total);
		}
	};

	const int threadCount = static_cast<int>(std::clamp<std::size_t>(
		static_cast<std::size_t>(std::max(1, plan.maxConcurrent)), 1, std::max<std::size_t>(1, toDownload.size())));
	std::vector<std::thread> threads;
	for (int i = 0; i < threadCount; ++i)
		threads.emplace_back(worker);
	for (std::thread& thread : threads)
		thread.join();

	if (cancel && cancel->load()) {
		if (err) *err = "Cancelled";
		return false;
	}
	if (failed.load()) {
		if (err) *err = firstError;
		return false;
	}

	// Files the previously installed version had and this one doesn't (e.g. mods dropped
	// when switching versions). Only files a plan installed by name are listed.
	{
		const std::vector<std::string> current = InstalledFiles(plan);
		for (const std::string& old : PreviousFiles(root / "pack.json")) {
			if (std::find(current.begin(), current.end(), old) == current.end() && IsSafeRelativePath(old))
				fs::remove(LongPath(gameDir / fs::u8path(old)), ec);
		}
	}

	// Overrides go last so the pack's own config files win over anything a mod zip shipped.
	for (std::size_t i = 0; i < plan.overrides.size(); ++i) {
		const OverrideZip& zip = plan.overrides[i];
		std::string error;
		auto zipProgress = [&](int count, int now) {
			report("Extracting pack files", static_cast<std::uint64_t>(now), static_cast<std::uint64_t>(count));
		};
		if (!ExtractZip(zip.zipPath, gameDir.string(), zip.prefix, zipProgress, &error, cancel)) {
			if (err) *err = "Extracting " + fs::path(zip.zipPath).filename().string() + " failed: " + error;
			return false;
		}
	}

	fs::remove_all(cacheDir, ec);
	if (!WritePackJson(plan, (root / "pack.json").string(), blocked)) {
		if (err) *err = "Could not write pack.json";
		return false;
	}
	return true;
}

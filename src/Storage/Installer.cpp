#include "Installer.h"
#include <miniz/miniz.h>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Returns false for entry names that are absolute or climb out of the target
// directory (e.g. "../evil.dll", "C:/evil.dll", "a/../../evil.dll").
static bool IsSafeEntryPath(const fs::path& entry)
{
	if (entry.empty() || entry.has_root_name() || entry.has_root_directory())
		return false;
	const fs::path normalized = entry.lexically_normal();
	return normalized.empty() || *normalized.begin() != "..";
}

bool InstallZip(const std::string& zipPath,
				const std::string& targetDir,
				std::function<void(int,int)> progress,
				std::string* err,
				const std::atomic<bool>* cancel)
{
	mz_zip_archive zip;
	memset(&zip, 0, sizeof(zip));
	if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) {
		if (err) *err = "Failed to open zip file";
		return false;
	}

	int num = (int)mz_zip_reader_get_num_files(&zip);
	int extracted = 0;

	for (int i = 0; i < num; ++i) {
		if (cancel && cancel->load()) {
			if (err) *err = "Cancelled";
			mz_zip_reader_end(&zip);
			return false;
		}

		mz_zip_archive_file_stat stat;
		if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
			// skip
			continue;
		}

		std::string filename = stat.m_filename ? stat.m_filename : std::string();
		if (!IsSafeEntryPath(fs::path(filename))) {
			if (err) *err = "Unsafe path in zip: " + filename;
			mz_zip_reader_end(&zip);
			return false;
		}
		fs::path outPath = fs::path(targetDir) / fs::path(filename).lexically_normal();

		if (mz_zip_reader_is_file_a_directory(&zip, i)) {
			fs::create_directories(outPath);
			++extracted;
			if (progress) progress(num, extracted);
			continue;
		}

		// ensure parent dir
		if (outPath.has_parent_path()) fs::create_directories(outPath.parent_path());

		if (!mz_zip_reader_extract_to_file(&zip, i, outPath.string().c_str(), 0)) {
			if (err) *err = "Failed to extract file: " + filename;
			mz_zip_reader_end(&zip);
			return false;
		}

		++extracted;
		if (progress) progress(num, extracted);
	}

	mz_zip_reader_end(&zip);
	return true;
}

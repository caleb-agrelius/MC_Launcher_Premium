#include "Installer.h"
#include "Util/Paths.h"
#include "Util/ZipArchive.h"
#include <filesystem>
#include <string>
#include <utility>

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

bool IsSafeRelativePath(const std::string& path)
{
	return path.empty() || IsSafeEntryPath(fs::path(path));
}

bool InstallZip(const std::string& zipPath,
				const std::string& targetDir,
				std::function<void(int,int)> progress,
				std::string* err,
				const std::atomic<bool>* cancel)
{
	return ExtractZip(zipPath, targetDir, "", std::move(progress), err, cancel);
}

bool ExtractZip(const std::string& zipPath,
				const std::string& targetDir,
				const std::string& prefix,
				std::function<void(int,int)> progress,
				std::string* err,
				const std::atomic<bool>* cancel)
{
	ZipReader reader;
	if (!reader.Open(zipPath)) {
		if (err) *err = "Failed to open zip file";
		return false;
	}
	mz_zip_archive* zip = reader.Raw();

	const int num = static_cast<int>(mz_zip_reader_get_num_files(zip));
	int extracted = 0;

	for (int i = 0; i < num; ++i) {
		if (cancel && cancel->load()) {
			if (err) *err = "Cancelled";
			return false;
		}

		mz_zip_archive_file_stat stat;
		if (!mz_zip_reader_file_stat(zip, i, &stat)) {
			// skip
			continue;
		}

		std::string filename = stat.m_filename ? stat.m_filename : std::string();
		if (!prefix.empty()) {
			if (filename.compare(0, prefix.size(), prefix) != 0) {
				++extracted;
				if (progress) progress(num, extracted);
				continue;
			}
			filename.erase(0, prefix.size());
			if (filename.empty()) {
				++extracted; // the prefix folder itself
				if (progress) progress(num, extracted);
				continue;
			}
		}
		if (!IsSafeEntryPath(fs::path(filename))) {
			if (err) *err = "Unsafe path in zip: " + filename;
			return false;
		}
		// LongPath: pack folders nest deeply, past Windows' 260-character limit.
		fs::path outPath = LongPath(fs::path(targetDir) / fs::path(filename).lexically_normal());

		if (mz_zip_reader_is_file_a_directory(zip, i)) {
			fs::create_directories(outPath);
			++extracted;
			if (progress) progress(num, extracted);
			continue;
		}

		// ensure parent dir
		if (outPath.has_parent_path()) fs::create_directories(outPath.parent_path());

		FILE* out = OpenFile(outPath, "wb");
		const bool extractedOk = out && mz_zip_reader_extract_to_cfile(zip, i, out, 0);
		if (out)
			fclose(out);
		if (!extractedOk) {
			if (err) *err = "Failed to extract file: " + filename;
			return false;
		}

		++extracted;
		if (progress) progress(num, extracted);
	}

	return true;
}

bool ReadZipEntry(const std::string& zipPath,
				  const std::string& entryName,
				  std::string& out,
				  std::string* err)
{
	out.clear();
	ZipReader zip;
	if (!zip.Open(zipPath)) {
		if (err) *err = "Failed to open zip file";
		return false;
	}
	if (mz_zip_reader_locate_file(zip.Raw(), entryName.c_str(), nullptr, 0) < 0) {
		if (err) *err = entryName + " not found in zip";
		return false;
	}
	if (!zip.ReadEntry(entryName.c_str(), out)) {
		if (err) *err = "Failed to read " + entryName;
		return false;
	}
	return true;
}

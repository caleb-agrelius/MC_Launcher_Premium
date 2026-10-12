#include "JarMods.h"

#include "Util/ZipArchive.h"
#include "Util/TracedFs.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>

namespace fs = std::filesystem;
using json = nlohmann::json;

// A loader's own jar (it ships a version.json), not a mod to patch into the game.
static bool IsLoaderJar(const fs::path& path)
{
	ZipReader zip;
	return zip.Open(path) && mz_zip_reader_locate_file(zip.Raw(), "version.json", nullptr, 0) >= 0;
}

std::vector<fs::path> FindJarMods(const fs::path& installDir, const fs::path& gameDir)
{
	const fs::path folder = gameDir / "jarmods";
	std::error_code ec;
	if (!fs::is_directory(folder, ec))
		return {};
	std::set<std::string> present;
	for (const auto& entry : fs::directory_iterator(folder, ec)) {
		if (entry.is_regular_file(ec) && !IsLoaderJar(entry.path()))
			present.insert(entry.path().filename().u8string());
	}

	// The order the pack listed them in (pack.json "files", e.g. "jarmods/forge.zip").
	std::vector<fs::path> ordered;
	std::set<std::string> used;
	try {
		std::ifstream in(installDir / "pack.json");
		json j;
		if (in.is_open() && (in >> j)) {
			for (const auto& file : j.value("files", json::array())) {
				if (!file.is_string())
					continue;
				const fs::path relative = fs::u8path(file.get<std::string>());
				const std::string name = relative.filename().u8string();
				if (relative.parent_path() == "jarmods" && present.count(name) && used.insert(name).second)
					ordered.push_back(folder / relative.filename());
			}
		}
	}
	catch (...) {
		// no usable order: fall back to names
	}
	for (const std::string& name : present) {
		if (used.insert(name).second)
			ordered.push_back(folder / fs::u8path(name));
	}
	return ordered;
}

// Name, size and modification time of every input, so a rebuild only happens when one changed.
static std::string InputsSignature(const fs::path& clientJar, const std::vector<fs::path>& jarMods)
{
	std::string signature;
	auto add = [&](const fs::path& path) {
		std::error_code ec;
		signature += path.u8string() + "|" + std::to_string(fs::file_size(path, ec)) + "|" +
			std::to_string(fs::last_write_time(path, ec).time_since_epoch().count()) + "\n";
	};
	add(clientJar);
	for (const fs::path& mod : jarMods)
		add(mod);
	return signature;
}

bool BuildPatchedJar(const fs::path& clientJar, const std::vector<fs::path>& jarMods, const fs::path& output, std::string* err)
{
	const fs::path inputsFile = fs::path(output).concat(".inputs");
	const std::string signature = InputsSignature(clientJar, jarMods);
	std::error_code ec;
	if (fs::is_regular_file(output, ec)) {
		std::ifstream in(inputsFile, std::ios::binary);
		const std::string previous((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (previous == signature)
			return true;
	}

	// Open every input; readers[0] is the game jar, then the jar mods in order.
	std::vector<fs::path> inputs = { clientJar };
	inputs.insert(inputs.end(), jarMods.begin(), jarMods.end());
	std::vector<std::unique_ptr<ZipReader>> readers;
	for (const fs::path& input : inputs) {
		auto reader = std::make_unique<ZipReader>();
		if (!reader->Open(input)) {
			if (err) *err = "Couldn't read " + input.filename().string() + " as a zip";
			return false;
		}
		readers.push_back(std::move(reader));
	}

	// Each entry name comes from the last input that has it; the first input to have a name
	// decides its position, so the output keeps the game jar's order.
	std::vector<std::string> order;
	std::map<std::string, std::pair<std::size_t, mz_uint>> source;
	for (std::size_t r = 0; r < readers.size(); ++r) {
		const mz_uint count = mz_zip_reader_get_num_files(readers[r]->Raw());
		for (mz_uint i = 0; i < count; ++i) {
			if (mz_zip_reader_is_file_a_directory(readers[r]->Raw(), i))
				continue;
			char name[1024];
			if (!mz_zip_reader_get_filename(readers[r]->Raw(), i, name, sizeof(name)))
				continue;
			const std::string entry = name;
			if (entry.rfind("META-INF/", 0) == 0)
				continue; // signatures of the original jar would reject the patched classes
			if (!source.count(entry))
				order.push_back(entry);
			source[entry] = { r, i };
		}
	}

	std::error_code dirError;
	TracedFs::CreateDirectories(output.parent_path(), dirError);
	const fs::path temp = fs::path(output).concat(".tmp");
	bool ok = true;
	{
		ZipWriter writer;
		if (!writer.Open(temp)) {
			if (err) *err = "Couldn't create " + temp.string();
			return false;
		}
		for (const std::string& entry : order) {
			const auto& [r, i] = source[entry];
			if (!writer.AddFromReader(*readers[r], i)) {
				ok = false;
				if (err) *err = "Couldn't copy " + entry + " from " + inputs[r].filename().string();
				break;
			}
		}
		ok = ok && writer.Finalize();
	} // the writer closes the file before it is renamed
	if (!ok) {
		TracedFs::Remove(temp, ec);
		if (err && err->empty()) *err = "Couldn't write the patched game jar";
		return false;
	}
	TracedFs::Rename(temp, output, ec);
	if (ec) {
		TracedFs::Remove(temp, ec);
		if (err) *err = "Couldn't save " + output.string() + ": " + ec.message();
		return false;
	}
	TracedFs::OpenWrite(inputsFile, std::ios::binary | std::ios::trunc) << signature;
	return true;
}

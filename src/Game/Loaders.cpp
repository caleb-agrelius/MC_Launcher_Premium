#include "Loaders.h"

#include "Network/HttpClient.h"
#include "Util/Process.h"

#include <miniz/miniz.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

const char* LoaderName(LoaderKind kind)
{
	switch (kind) {
	case LoaderKind::Fabric: return "Fabric";
	case LoaderKind::Quilt: return "Quilt";
	case LoaderKind::Forge: return "Forge";
	case LoaderKind::NeoForge: return "NeoForge";
	default: return "Vanilla";
	}
}

static fs::path VersionJsonPath(const fs::path& root, const std::string& id)
{
	return root / "versions" / id / (id + ".json");
}

// Fabric and Quilt publish a ready-made version JSON that inherits from vanilla.
static bool InstallMetaProfile(HttpClient& http, const fs::path& root, const std::string& metaUrl,
	const std::string& expectedId, std::string& versionId, std::string* err, const std::atomic<bool>* cancel)
{
	std::error_code ec;
	if (fs::is_regular_file(VersionJsonPath(root, expectedId), ec)) {
		versionId = expectedId;
		return true;
	}
	std::string body;
	if (!http.Get(metaUrl, body, err, cancel))
		return false;
	json profile;
	try {
		profile = json::parse(body);
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Bad loader profile: ") + ex.what();
		return false;
	}
	versionId = profile.value("id", expectedId);
	const fs::path path = VersionJsonPath(root, versionId);
	fs::create_directories(path.parent_path(), ec);
	std::ofstream ofs(path, std::ios::trunc);
	ofs << profile.dump(2) << "\n";
	if (!ofs.good()) {
		if (err) *err = "Couldn't write " + path.string();
		return false;
	}
	return true;
}

// Reads the version id from version.json inside a Forge/NeoForge installer jar.
static bool ReadInstallerVersionId(const fs::path& installer, std::string& id, std::string* err)
{
	mz_zip_archive zip;
	std::memset(&zip, 0, sizeof(zip));
	if (!mz_zip_reader_init_file(&zip, installer.string().c_str(), 0)) {
		if (err) *err = "Couldn't open " + installer.filename().string();
		return false;
	}
	std::size_t size = 0;
	void* data = mz_zip_reader_extract_file_to_heap(&zip, "version.json", &size, 0);
	mz_zip_reader_end(&zip);
	if (!data) {
		if (err) *err = "This Forge version is too old to install automatically (its installer has no version.json)";
		return false;
	}
	const std::string text(static_cast<const char*>(data), size);
	mz_free(data);
	try {
		id = json::parse(text).value("id", "");
	}
	catch (...) {
		id.clear();
	}
	if (id.empty()) {
		if (err) *err = "Couldn't read the version id from " + installer.filename().string();
		return false;
	}
	return true;
}

// True when the version JSON exists and every library it needs is on disk
// (Forge's installer generates some of them, so they have no download URL).
static bool ForgeAlreadyInstalled(const fs::path& root, const std::string& id)
{
	ResolvedVersion version;
	if (!ResolveVersion(root, id, version, nullptr))
		return false;
	for (const LibraryFile& file : version.classpath) {
		std::error_code ec;
		if (file.url.empty() && !fs::is_regular_file(file.path, ec))
			return false;
	}
	return true;
}

static bool InstallForgeLike(HttpClient& http, const fs::path& root, LoaderKind kind, const std::string& minecraftVersion,
	const std::string& loaderVersion, const fs::path& javaExe, std::string& versionId, const ProgressFn& progress,
	std::string* err, const std::atomic<bool>* cancel)
{
	std::string url;
	std::string fileName;
	if (kind == LoaderKind::Forge) {
		const std::string full = minecraftVersion + "-" + loaderVersion;
		fileName = "forge-" + full + "-installer.jar";
		url = "https://maven.minecraftforge.net/net/minecraftforge/forge/" + full + "/" + fileName;
	} else if (minecraftVersion == "1.20.1") {
		// NeoForge's only 1.20.1 releases still used Forge's artifact name.
		const std::string full = "1.20.1-" + loaderVersion;
		fileName = "forge-" + full + "-installer.jar";
		url = "https://maven.neoforged.net/releases/net/neoforged/forge/" + full + "/" + fileName;
	} else {
		fileName = "neoforge-" + loaderVersion + "-installer.jar";
		url = "https://maven.neoforged.net/releases/net/neoforged/neoforge/" + loaderVersion + "/" + fileName;
	}

	DownloadJob job;
	job.url = url;
	job.path = root / "installers" / fileName;
	if (!DownloadAll(http, { job }, std::string("Downloading ") + LoaderName(kind) + " installer", progress, err, cancel))
		return false;
	if (!ReadInstallerVersionId(job.path, versionId, err))
		return false;
	if (ForgeAlreadyInstalled(root, versionId))
		return true;

	// The installer refuses to run without the official launcher's profile file.
	const fs::path profiles = root / "launcher_profiles.json";
	std::error_code ec;
	if (!fs::exists(profiles, ec))
		std::ofstream(profiles) << "{\"profiles\":{}}\n";

	if (progress)
		progress(std::string("Running the ") + LoaderName(kind) + " installer (this can take a few minutes)", -1.0);
	const fs::path log = root / "logs" / "loader-installer.log";
	// Forge uses --installClient; some NeoForge installers only accept --install-client.
	for (const char* flag : { "--installClient", "--install-client" }) {
		int exitCode = -1;
		std::string error;
		if (!RunProcess({ javaExe.string(), "-jar", job.path.string(), flag, root.string() }, root, log, true,
				&exitCode, &error, cancel, true)) {
			if (err) *err = error;
			return false;
		}
		if (exitCode == 0 && ForgeAlreadyInstalled(root, versionId))
			return true;
	}
	if (err) *err = std::string("The ") + LoaderName(kind) + " installer failed. See " + log.string();
	return false;
}

bool InstallLoader(HttpClient& http, const fs::path& root, LoaderKind kind, const std::string& minecraftVersion,
	const std::string& loaderVersion, const fs::path& javaExe, std::string& versionId, const ProgressFn& progress,
	std::string* err, const std::atomic<bool>* cancel)
{
	switch (kind) {
	case LoaderKind::Vanilla:
		versionId = minecraftVersion;
		return true;
	case LoaderKind::Fabric:
		if (progress) progress("Installing Fabric " + loaderVersion, -1.0);
		return InstallMetaProfile(http, root,
			"https://meta.fabricmc.net/v2/versions/loader/" + minecraftVersion + "/" + loaderVersion + "/profile/json",
			"fabric-loader-" + loaderVersion + "-" + minecraftVersion, versionId, err, cancel);
	case LoaderKind::Quilt:
		if (progress) progress("Installing Quilt " + loaderVersion, -1.0);
		return InstallMetaProfile(http, root,
			"https://meta.quiltmc.org/v3/versions/loader/" + minecraftVersion + "/" + loaderVersion + "/profile/json",
			"quilt-loader-" + loaderVersion + "-" + minecraftVersion, versionId, err, cancel);
	case LoaderKind::Forge:
	case LoaderKind::NeoForge:
		return InstallForgeLike(http, root, kind, minecraftVersion, loaderVersion, javaExe, versionId, progress, err, cancel);
	}
	return false;
}

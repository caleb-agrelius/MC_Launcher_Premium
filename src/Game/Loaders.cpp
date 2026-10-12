#include "Loaders.h"

#include "Network/HttpClient.h"
#include "Util/Process.h"

#include "Util/ZipArchive.h"
#include "Util/TracedFs.h"
#include <nlohmann/json.hpp>

#include <cstdlib>
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
static bool InstallMetaProfile(IHttpClient& http, const fs::path& root, const std::string& metaUrl,
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
	TracedFs::CreateDirectories(path.parent_path(), ec);
	auto ofs = TracedFs::OpenWrite(path, std::ios::trunc);
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
	ZipReader zip;
	if (!zip.Open(installer)) {
		if (err) *err = "Couldn't open " + installer.filename().string();
		return false;
	}
	std::string text;
	if (!zip.ReadEntry("version.json", text)) {
		if (err) *err = "This Forge version is too old to install automatically (its installer has no version.json)";
		return false;
	}
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

bool DownloadForgeInstaller(IHttpClient& http, const fs::path& root, LoaderKind kind, const std::string& minecraftVersion,
	const std::string& loaderVersion, fs::path& installer, const ProgressFn& progress, std::string* err,
	const std::atomic<bool>* cancel)
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
	installer = job.path;
	return DownloadAll(http, { job }, std::string("Downloading ") + LoaderName(kind) + " installer", progress, err, cancel);
}

static bool InstallForgeLike(IHttpClient& http, const fs::path& root, LoaderKind kind, const std::string& minecraftVersion,
	const std::string& loaderVersion, const fs::path& javaExe, std::string& versionId, const ProgressFn& progress,
	std::string* err, const std::atomic<bool>* cancel)
{
	DownloadJob job;
	if (!DownloadForgeInstaller(http, root, kind, minecraftVersion, loaderVersion, job.path, progress, err, cancel))
		return false;
	if (!ReadInstallerVersionId(job.path, versionId, err))
		return false;
	if (ForgeAlreadyInstalled(root, versionId))
		return true;

	// The installer refuses to run without the official launcher's profile file.
	const fs::path profiles = root / "launcher_profiles.json";
	std::error_code ec;
	if (!fs::exists(profiles, ec))
		TracedFs::OpenWrite(profiles) << "{\"profiles\":{}}\n";

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

bool IsLegacyForgeMinecraft(const std::string& minecraftVersion)
{
	// "1.12.2" -> 12; anything not shaped like 1.x is treated as modern.
	if (minecraftVersion.rfind("1.", 0) != 0)
		return false;
	const int minor = std::atoi(minecraftVersion.c_str() + 2);
	return minor > 0 && minor < 13;
}

bool NeedsLegacyJavaFixer(const std::string& minecraftVersion, const std::string& forgeVersion)
{
	if (minecraftVersion.rfind("1.6", 0) == 0 || minecraftVersion == "1.7.2")
		return true;
	if (minecraftVersion != "1.7.10")
		return false;
	// 1.7.10 builds from 10.13.2.1230 on include the fix; the build is the last number.
	const std::size_t dot = forgeVersion.find_last_of('.');
	const int build = std::atoi(forgeVersion.c_str() + (dot == std::string::npos ? 0 : dot + 1));
	return build > 0 && build < 1230;
}

DownloadJob LegacyJavaFixerJob(const fs::path& gameDir)
{
	DownloadJob job;
	job.url = "https://maven.minecraftforge.net/net/minecraftforge/lex/legacyjavafixer/1.0/legacyjavafixer-1.0.jar";
	job.sha1 = "a11b502bef19f49bfc199722b94da5f3d7b470a8";
	job.size = 6245;
	job.path = gameDir / "mods" / "legacyjavafixer-1.0.jar";
	return job;
}

bool InstallForgeFromJar(const fs::path& root, const fs::path& forgeJar, const std::string& minecraftVersion,
	std::string& versionId, std::string* err)
{
	ZipReader zip;
	if (!zip.Open(forgeJar)) {
		if (err) *err = "Couldn't open " + forgeJar.filename().string();
		return false;
	}
	std::string versionText;
	if (!zip.ReadEntry("version.json", versionText)) {
		if (err) *err = forgeJar.filename().string() + " has no version.json, so it isn't a Forge universal jar";
		return false;
	}
	json version;
	try {
		version = json::parse(versionText);
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Couldn't read Forge's version.json: ") + ex.what();
		return false;
	}
	versionId = version.value("id", "");
	if (versionId.empty() || versionId.find_first_of("/\\:") != std::string::npos) {
		if (err) *err = "Forge's version.json has no usable id";
		return false;
	}
	// Old Forge version files list every library themselves; inherit the rest from vanilla.
	if (!version.contains("inheritsFrom"))
		version["inheritsFrom"] = minecraftVersion;
	if (!version.contains("jar"))
		version["jar"] = minecraftVersion;

	json libraries = json::array();
	std::string forgeLibrary;
	for (json library : version.value("libraries", json::array())) {
		if (library.contains("clientreq") && !library.value("clientreq", true))
			continue; // server-only
		const std::string name = library.value("name", "");
		if (name.rfind("net.minecraftforge:forge:", 0) == 0 || name.rfind("net.minecraftforge:minecraftforge:", 0) == 0) {
			// Forge itself: this very jar, copied in below (nothing to download).
			forgeLibrary = name;
			library = { { "name", name }, { "downloads", { { "artifact", { { "path", MavenPath(name) }, { "url", "" } } } } } };
		} else if (library.contains("url") && library["url"].is_string()) {
			std::string url = library["url"].get<std::string>();
			for (const char* old : { "http://files.minecraftforge.net/maven/", "https://files.minecraftforge.net/maven/" }) {
				if (url.rfind(old, 0) == 0)
					url = "https://maven.minecraftforge.net/" + url.substr(std::strlen(old));
			}
			if (url.rfind("http://", 0) == 0)
				url = "https://" + url.substr(7);
			library["url"] = url;
		}
		library.erase("checksums");
		libraries.push_back(std::move(library));
	}
	if (forgeLibrary.empty()) {
		if (err) *err = "Forge's version.json doesn't list Forge itself";
		return false;
	}
	version["libraries"] = libraries;

	std::error_code ec;
	const fs::path library = root / "libraries" / fs::u8path(MavenPath(forgeLibrary));
	TracedFs::CreateDirectories(library.parent_path(), ec);
	TracedFs::CopyFile(forgeJar, library, fs::copy_options::overwrite_existing, ec);
	if (ec) {
		if (err) *err = "Couldn't copy Forge into the libraries folder: " + ec.message();
		return false;
	}
	const fs::path path = VersionJsonPath(root, versionId);
	TracedFs::CreateDirectories(path.parent_path(), ec);
	auto ofs = TracedFs::OpenWrite(path, std::ios::trunc);
	ofs << version.dump(2) << "\n";
	if (!ofs.good()) {
		if (err) *err = "Couldn't write " + path.string();
		return false;
	}
	return true;
}

bool InstallLoader(IHttpClient& http, const fs::path& root, LoaderKind kind, const std::string& minecraftVersion,
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

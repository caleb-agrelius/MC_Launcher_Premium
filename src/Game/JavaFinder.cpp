#include "JavaFinder.h"
#include "Util/Text.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

#ifdef _WIN32
static const char* kJavaExe = "java.exe";
static const char* kJavawExe = "javaw.exe";
#else
static const char* kJavaExe = "java";
static const char* kJavawExe = "java";
#endif

const char* JavaExecutableName(bool windowed)
{
	return windowed ? kJavawExe : kJavaExe;
}

// Version numbers as a list of integers, for ordering: "1.8.0_382" -> 1 8 0 382.
static std::vector<int> VersionParts(const std::string& version)
{
	std::vector<int> parts;
	int value = -1;
	for (char c : version) {
		if (std::isdigit(static_cast<unsigned char>(c))) {
			value = (value < 0 ? 0 : value) * 10 + (c - '0');
			if (value > 100000000)
				value = 100000000;
		} else if (value >= 0) {
			parts.push_back(value);
			value = -1;
		}
	}
	if (value >= 0)
		parts.push_back(value);
	return parts;
}

int JavaMajorVersion(const std::string& version)
{
	const std::vector<int> parts = VersionParts(version);
	if (parts.empty())
		return 0;
	// Java 8 and older call themselves 1.x.
	if (parts[0] == 1 && parts.size() > 1)
		return parts[1];
	return parts[0];
}

// The first run of digits and dots in a folder name: "jdk-17.0.2" -> "17.0.2".
static std::string VersionInName(const std::string& name)
{
	std::size_t start = 0;
	while (start < name.size() && !std::isdigit(static_cast<unsigned char>(name[start])))
		++start;
	std::size_t end = start;
	while (end < name.size() && (std::isdigit(static_cast<unsigned char>(name[end])) || name[end] == '.' || name[end] == '_'))
		++end;
	return name.substr(start, end - start);
}

// Mojang names its runtimes after Greek letters rather than versions.
static int MojangComponentMajor(const std::string& name)
{
	static const std::pair<const char*, int> kComponents[] = {
		{ "jre-legacy", 8 }, { "java-runtime-alpha", 16 }, { "java-runtime-beta", 17 },
		{ "java-runtime-gamma", 17 }, { "java-runtime-gamma-snapshot", 17 }, { "java-runtime-delta", 21 },
		{ "java-runtime-epsilon", 25 },
	};
	for (const auto& [component, major] : kComponents) {
		if (name == component)
			return major;
	}
	return 0;
}

static std::string Unquote(std::string value)
{
	while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
		value.pop_back();
	if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
		value = value.substr(1, value.size() - 2);
	return value;
}

bool ReadJavaInstall(const fs::path& home, JavaInstall& out)
{
	std::error_code ec;
	const fs::path bin = home / "bin";
	if (!fs::is_regular_file(bin / kJavaExe, ec))
		return false;
	JavaInstall java;
	java.home = home;
	java.java = bin / kJavaExe;
	java.javaw = fs::is_regular_file(bin / kJavawExe, ec) ? bin / kJavawExe : java.java;
	java.mojang = MojangComponentMajor(home.filename().string()) != 0; // also Prism's copies

	std::string arch;
	std::ifstream release(home / "release");
	for (std::string line; std::getline(release, line);) {
		const std::size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		const std::string key = line.substr(0, equals);
		const std::string value = Unquote(line.substr(equals + 1));
		if (key == "JAVA_VERSION")
			java.version = value;
		else if (key == "IMPLEMENTOR")
			java.vendor = value;
		else if (key == "OS_ARCH")
			arch = value;
	}
	// 32-bit Java can't give Minecraft more than about 1.5 GB.
	if (arch == "x86" || arch == "i386" || arch == "i586" || arch == "i686")
		return false;
	if (java.version.empty()) {
		// No release file: a Mojang runtime's component, or the version in the folder name.
		const std::string name = home.filename().string();
		const int component = MojangComponentMajor(name);
		java.version = component ? std::to_string(component) : VersionInName(name);
	}
	java.major = JavaMajorVersion(java.version);
	if (java.major <= 0)
		return false;
	out = java;
	return true;
}

// Adds the Java homes in folder, looking up to depth levels of subfolders. A folder that is
// a Java home isn't searched further.
static void ScanFolder(const fs::path& folder, int depth, bool mojang, std::vector<JavaInstall>& out)
{
	std::error_code ec;
	if (!fs::is_directory(folder, ec))
		return;
	JavaInstall java;
	if (ReadJavaInstall(folder, java)) {
		java.mojang = java.mojang || mojang;
		out.push_back(java);
		return;
	}
	if (depth <= 0)
		return;
	for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
		it.increment(ec)) {
		if (it->is_directory(ec))
			ScanFolder(it->path(), depth - 1, mojang, out);
	}
}

std::vector<JavaInstall> ScanJavaFolders(const std::vector<fs::path>& folders)
{
	std::vector<JavaInstall> found;
	for (const fs::path& folder : folders)
		ScanFolder(folder, 2, false, found);
	return found;
}

static fs::path EnvPath(const char* name)
{
#ifdef _WIN32
	std::wstring wide(name, name + std::strlen(name));
	const DWORD length = GetEnvironmentVariableW(wide.c_str(), nullptr, 0);
	if (length == 0)
		return {};
	std::wstring value(length, L'\0');
	value.resize(GetEnvironmentVariableW(wide.c_str(), value.data(), length));
	return fs::path(value);
#else
	const char* value = std::getenv(name);
	return value ? fs::path(value) : fs::path();
#endif
}

#ifdef _WIN32
// Values naming a Java home under a vendor's registry key, searched a few levels deep:
// Oracle's JavaHome, and the Path the Adoptium, Microsoft and Zulu installers write.
static void RegistryHomes(HKEY root, const std::wstring& key, int depth, std::vector<fs::path>& out)
{
	HKEY handle = nullptr;
	if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &handle) != ERROR_SUCCESS)
		return;
	for (const wchar_t* valueName : { L"JavaHome", L"Path", L"InstallationPath" }) {
		wchar_t value[1024];
		DWORD size = sizeof(value);
		DWORD type = 0;
		if (RegQueryValueExW(handle, valueName, nullptr, &type, reinterpret_cast<BYTE*>(value), &size) == ERROR_SUCCESS &&
			(type == REG_SZ || type == REG_EXPAND_SZ) && size > sizeof(wchar_t)) {
			value[std::min<std::size_t>(size / sizeof(wchar_t), 1023)] = L'\0';
			out.push_back(fs::path(value));
		}
	}
	if (depth > 0) {
		for (DWORD index = 0;; ++index) {
			wchar_t name[256];
			DWORD length = 256;
			if (RegEnumKeyExW(handle, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
				break;
			RegistryHomes(root, key + L"\\" + name, depth - 1, out);
		}
	}
	RegCloseKey(handle);
}
#endif

std::vector<JavaInstall> FindJavaInstalls(const fs::path& gameRoot)
{
	std::vector<JavaInstall> found;
	// Single homes: JAVA_HOME, PATH, the registry.
	std::vector<fs::path> homes;
	if (const fs::path javaHome = EnvPath("JAVA_HOME"); !javaHome.empty())
		homes.push_back(javaHome);
#ifdef _WIN32
	const std::wstring path = EnvPath("PATH").wstring();
	for (std::size_t start = 0; start <= path.size();) {
		std::size_t end = path.find(L';', start);
		if (end == std::wstring::npos)
			end = path.size();
		const fs::path dir(path.substr(start, end - start));
		if (!dir.empty() && dir.filename() == "bin")
			homes.push_back(dir.parent_path());
		start = end + 1;
	}
	for (const wchar_t* vendor : { L"SOFTWARE\\JavaSoft", L"SOFTWARE\\Eclipse Adoptium", L"SOFTWARE\\Eclipse Foundation",
			L"SOFTWARE\\AdoptOpenJDK", L"SOFTWARE\\Microsoft\\JDK", L"SOFTWARE\\Azul Systems\\Zulu",
			L"SOFTWARE\\BellSoft\\Liberica", L"SOFTWARE\\Amazon Corretto" }) {
		RegistryHomes(HKEY_LOCAL_MACHINE, vendor, 4, homes);
		RegistryHomes(HKEY_CURRENT_USER, vendor, 4, homes);
	}
#else
	// A java on PATH is usually a link (/usr/bin/java -> /etc/alternatives/java -> the JDK's bin/java),
	// so the home is wherever the link finally points.
	const std::string path = EnvPath("PATH").string();
	for (std::size_t start = 0; start <= path.size();) {
		std::size_t end = path.find(':', start);
		if (end == std::string::npos)
			end = path.size();
		if (end > start) {
			std::error_code ec;
			const fs::path java = fs::canonical(fs::path(path.substr(start, end - start)) / kJavaExe, ec);
			if (!ec)
				homes.push_back(java.parent_path().parent_path());
		}
		start = end + 1;
	}
#endif
	for (const fs::path& home : homes)
		ScanFolder(home, 0, false, found);

	// Folders that hold Java homes.
	std::vector<fs::path> vendorFolders;
#ifndef _WIN32
	for (const char* folder : { "/usr/lib/jvm", "/usr/lib64/jvm", "/usr/java", "/opt/java", "/opt/jdks", "/opt" })
		vendorFolders.push_back(folder);
	if (const fs::path home = EnvPath("HOME"); !home.empty()) {
		vendorFolders.push_back(home / ".jdks");                       // IntelliJ's downloads
		vendorFolders.push_back(home / ".sdkman" / "candidates" / "java");
	}
#endif
	for (const char* programs : { "ProgramW6432", "ProgramFiles" }) {
		const fs::path base = EnvPath(programs);
		if (base.empty())
			continue;
		for (const char* vendor : { "Java", "Eclipse Adoptium", "Eclipse Foundation", "AdoptOpenJDK", "Microsoft", "Zulu",
				"BellSoft", "Amazon Corretto", "Semeru", "OpenJDK", "ojdkbuild" })
			vendorFolders.push_back(base / vendor);
	}
	if (const fs::path local = EnvPath("LOCALAPPDATA"); !local.empty()) {
		for (const char* vendor : { "Eclipse Adoptium", "Microsoft", "Java" })
			vendorFolders.push_back(local / "Programs" / vendor);
	}
	if (const fs::path profile = EnvPath("USERPROFILE"); !profile.empty())
		vendorFolders.push_back(profile / ".jdks"); // IntelliJ's downloads
	for (const fs::path& folder : vendorFolders)
		ScanFolder(folder, 1, false, found);

	// Mojang runtimes: this launcher's (only finished downloads), the official launcher's
	// (classic and Microsoft Store), and Prism's.
	std::error_code ec;
	for (fs::directory_iterator it(gameRoot / "runtime", ec), end; !ec && it != end; it.increment(ec)) {
		if (fs::is_regular_file(it->path() / ".manifest-sha1", ec))
			ScanFolder(it->path(), 0, true, found);
	}
	if (const fs::path appData = EnvPath("APPDATA"); !appData.empty()) {
		ScanFolder(appData / ".minecraft" / "runtime", 3, true, found);
		ScanFolder(appData / "PrismLauncher" / "java", 3, false, found);
	}
#ifndef _WIN32
	if (const fs::path home = EnvPath("HOME"); !home.empty()) {
		fs::path data = EnvPath("XDG_DATA_HOME");
		if (data.empty())
			data = home / ".local" / "share";
		ScanFolder(home / ".minecraft" / "runtime", 3, true, found);
		ScanFolder(data / "PrismLauncher" / "java", 3, false, found);
		// Prism as a Flatpak or Snap keeps its own data folder.
		ScanFolder(home / ".var" / "app" / "org.prismlauncher.PrismLauncher" / "data" / "PrismLauncher" / "java", 3, false, found);
		ScanFolder(home / ".var" / "app" / "com.mojang.Minecraft" / ".minecraft" / "runtime", 3, true, found);
		ScanFolder(home / "snap" / "prism-launcher" / "current" / ".local" / "share" / "PrismLauncher" / "java", 3, false, found);
	}
#endif
	if (const fs::path local = EnvPath("LOCALAPPDATA"); !local.empty())
		ScanFolder(local / "Packages" / "Microsoft.4297127D64EC6_8wekyb3d8bbwe" / "LocalCache" / "Local" / "runtime", 3, true, found);

	// Each home once (the first sighting wins), newest first.
	std::vector<JavaInstall> unique;
	std::set<std::string> seen;
	for (JavaInstall& java : found) {
		std::string key = fs::weakly_canonical(java.home, ec).generic_string();
		if (ec)
			key = java.home.generic_string();
		if (seen.insert(ToLower(key)).second)
			unique.push_back(std::move(java));
	}
	std::stable_sort(unique.begin(), unique.end(), [](const JavaInstall& a, const JavaInstall& b) {
		return VersionParts(a.version) > VersionParts(b.version);
	});
	return unique;
}

const JavaInstall* PickJava(const std::vector<JavaInstall>& installs, int requiredMajor)
{
	const JavaInstall* best = nullptr;
	for (const JavaInstall& java : installs) {
		if (java.major != requiredMajor)
			continue;
		if (!best) {
			best = &java;
			continue;
		}
		const std::vector<int> a = VersionParts(java.version);
		const std::vector<int> b = VersionParts(best->version);
		if (a > b || (a == b && java.mojang && !best->mojang))
			best = &java;
	}
	return best;
}

std::string JavaLabel(const JavaInstall& java)
{
	std::string label = "Java " + java.version;
	if (java.mojang)
		label += " (Mojang)";
	else if (!java.vendor.empty())
		label += " (" + java.vendor + ")";
	return label;
}

#include "InstanceContent.h"
#include "Util/Text.h"
#include "Util/TracedFs.h"

#include <miniz/miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char* kDisabledSuffix = ".disabled";

static bool EndsWith(const std::string& text, const std::string& suffix)
{
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

const ContentKindInfo& GetContentKind(ContentKind kind)
{
	static const ContentKindInfo mods{ "mods", "Mods", "mod", { ".jar" }, false };
	static const ContentKindInfo resourcePacks{ "resourcepacks", "Resource packs", "resource pack", { ".zip" }, true };
	static const ContentKindInfo shaderPacks{ "shaderpacks", "Shader packs", "shader pack", { ".zip" }, true };
	switch (kind) {
	case ContentKind::ResourcePacks: return resourcePacks;
	case ContentKind::ShaderPacks: return shaderPacks;
	default: return mods;
	}
}

std::vector<ContentFile> ListContent(const fs::path& folder, const std::vector<std::string>& extensions, bool folders)
{
	std::map<std::string, ContentFile> found; // by lowercase name, so the list is sorted ignoring case
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(folder, ec)) {
		const bool isFolder = entry.is_directory(ec);
		if (!isFolder && !entry.is_regular_file(ec))
			continue;
		if (isFolder && !folders)
			continue;
		std::string name = entry.path().filename().u8string();
		bool enabled = true;
		if (EndsWith(ToLower(name), kDisabledSuffix)) {
			name.erase(name.size() - std::strlen(kDisabledSuffix));
			enabled = false;
		}
		if (!isFolder) {
			const std::string lower = ToLower(name);
			if (std::none_of(extensions.begin(), extensions.end(), [&](const std::string& ext) { return EndsWith(lower, ext); }))
				continue;
		}
		ContentFile& file = found[ToLower(name)];
		if (file.fileName.empty() || enabled) {
			file.fileName = name;
			file.enabled = enabled;
			file.folder = isFolder;
			file.size = isFolder ? 0 : entry.file_size(ec);
		}
	}
	std::vector<ContentFile> out;
	out.reserve(found.size());
	for (auto& [key, file] : found)
		out.push_back(std::move(file));
	return out;
}

std::vector<ContentFile> ListContent(const fs::path& gameDir, ContentKind kind)
{
	const ContentKindInfo& info = GetContentKind(kind);
	return ListContent(gameDir / info.folder, info.extensions, info.folders);
}

bool IsPlainFileName(const std::string& name)
{
	return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string::npos;
}

bool SetContentEnabled(const fs::path& folder, const std::string& fileName, bool enabled, std::string* err)
{
	if (!IsPlainFileName(fileName)) {
		if (err) *err = "Not a file name: " + fileName;
		return false;
	}
	const fs::path on = folder / fs::u8path(fileName);
	fs::path off = on;
	off += kDisabledSuffix;
	const fs::path& from = enabled ? off : on;
	const fs::path& to = enabled ? on : off;
	std::error_code ec;
	if (!fs::exists(from, ec)) {
		if (fs::exists(to, ec))
			return true; // already in that state
		if (err) *err = fileName + " isn't in " + folder.filename().u8string();
		return false;
	}
	// Both copies (e.g. a pack update re-added a disabled mod): keep the one being switched to.
	if (fs::exists(to, ec))
		TracedFs::RemoveAll(from, ec);
	else
		TracedFs::Rename(from, to, ec);
	if (ec) {
		if (err) *err = "Couldn't rename " + fileName + ": " + ec.message() + ". Close the game or stop the server first.";
		return false;
	}
	return true;
}

bool DeleteContent(const fs::path& folder, const std::string& fileName, std::string* err)
{
	if (!IsPlainFileName(fileName)) {
		if (err) *err = "Not a file name: " + fileName;
		return false;
	}
	const fs::path on = folder / fs::u8path(fileName);
	fs::path off = on;
	off += kDisabledSuffix;
	std::error_code ec;
	for (const fs::path& path : { on, off }) {
		TracedFs::RemoveAll(path, ec);
		if (ec) {
			if (err) *err = "Couldn't delete " + fileName + ": " + ec.message() + ". Close the game or stop the server first.";
			return false;
		}
	}
	return true;
}

bool ContentKindForFile(const fs::path& source, ContentKind preferred, ContentKind* kind)
{
	std::error_code ec;
	const std::string lower = ToLower(source.filename().u8string());
	ContentKind found;
	if (EndsWith(lower, ".jar") && fs::is_regular_file(source, ec))
		found = ContentKind::Mods;
	else if ((EndsWith(lower, ".zip") && fs::is_regular_file(source, ec)) || fs::is_directory(source, ec))
		found = preferred == ContentKind::ShaderPacks ? ContentKind::ShaderPacks : ContentKind::ResourcePacks;
	else
		return false;
	if (kind)
		*kind = found;
	return true;
}

bool AddContent(const fs::path& gameDir, ContentKind kind, const fs::path& source, std::string* fileName, std::string* err)
{
	const ContentKindInfo& info = GetContentKind(kind);
	std::error_code ec;
	const bool isFolder = fs::is_directory(source, ec);
	const std::string name = source.filename().u8string();
	const std::string lower = ToLower(name);
	const bool typeOk = isFolder ? info.folders
		: fs::is_regular_file(source, ec) &&
			std::any_of(info.extensions.begin(), info.extensions.end(), [&](const std::string& ext) { return EndsWith(lower, ext); });
	if (!typeOk || !IsPlainFileName(name)) {
		if (err) *err = name + " isn't a " + info.singular + " (" + info.extensions.front() + (info.folders ? " file or folder)" : " file)");
		return false;
	}
	const fs::path folder = gameDir / info.folder;
	const fs::path target = folder / fs::u8path(name);
	fs::path off = target;
	off += kDisabledSuffix;
	if (fs::exists(target, ec) || fs::exists(off, ec)) {
		if (err) *err = name + " is already in " + info.folder + ".";
		return false;
	}
	TracedFs::CreateDirectories(folder, ec);
	if (isFolder)
		TracedFs::Copy(source, target, fs::copy_options::recursive, ec);
	else
		TracedFs::CopyFile(source, target, ec);
	if (ec) {
		TracedFs::RemoveAll(target, ec);
		if (err) *err = "Couldn't copy " + name + " into " + info.folder + ": " + ec.message();
		return false;
	}
	if (fileName)
		*fileName = name;
	return true;
}

std::string ContentPath(ContentKind kind, const std::string& fileName)
{
	return std::string(GetContentKind(kind).folder) + "/" + fileName;
}

void RecordContentChange(ContentChanges& changes, const std::string& path, ContentChange change)
{
	const auto it = changes.find(path);
	const std::string current = it != changes.end() ? it->second : std::string();
	auto set = [&](const char* value) { changes[path] = value; };
	auto clear = [&]() { if (it != changes.end()) changes.erase(it); };
	switch (change) {
	case ContentChange::Added:
		// The same name as a pack file the player removed: it's back, as far as anyone can tell.
		if (current == "removed") clear(); else set("added");
		break;
	case ContentChange::Removed:
		if (current == "added") clear(); else set("removed");
		break;
	case ContentChange::Disabled:
		if (current == "added") break; // still the player's own file
		if (current == "enabled") clear(); else set("disabled");
		break;
	case ContentChange::Enabled:
		if (current == "added") break;
		if (current == "disabled") clear(); else set("enabled");
		break;
	}
}

std::string DescribeContentChanges(const ContentChanges& changes)
{
	int added = 0, removed = 0, off = 0, on = 0;
	for (const auto& [path, change] : changes) {
		if (change == "added") ++added;
		else if (change == "removed") ++removed;
		else if (change == "disabled") ++off;
		else if (change == "enabled") ++on;
	}
	std::string text;
	auto part = [&](int n, const char* what) {
		if (n > 0)
			text += (text.empty() ? "" : ", ") + std::to_string(n) + " " + what;
	};
	part(added, "added");
	part(removed, "removed");
	part(off, "turned off");
	part(on, "turned on");
	return text;
}

std::set<std::string> RemovedContent(const ContentChanges& changes)
{
	std::set<std::string> removed;
	for (const auto& [path, change] : changes) {
		if (change == "removed")
			removed.insert(path);
	}
	return removed;
}

// Splits "mods/sodium.jar" into the folder and a plain file name; false for anything else.
static bool SplitContentPath(const fs::path& gameDir, const std::string& path, fs::path* folder, std::string* fileName)
{
	const std::size_t slash = path.find('/');
	if (slash == std::string::npos)
		return false;
	const std::string dir = path.substr(0, slash);
	*fileName = path.substr(slash + 1);
	for (ContentKind kind : { ContentKind::Mods, ContentKind::ResourcePacks, ContentKind::ShaderPacks }) {
		if (dir == GetContentKind(kind).folder && IsPlainFileName(*fileName)) {
			*folder = gameDir / dir;
			return true;
		}
	}
	return false;
}

void ApplyContentChanges(const fs::path& gameDir, const ContentChanges& changes)
{
	for (const auto& [path, change] : changes) {
		fs::path folder;
		std::string fileName;
		if (!SplitContentPath(gameDir, path, &folder, &fileName))
			continue;
		std::string ignored;
		if (change == "removed")
			DeleteContent(folder, fileName, &ignored);
		else if (change == "disabled")
			SetContentEnabled(folder, fileName, false, &ignored);
		else if (change == "enabled")
			SetContentEnabled(folder, fileName, true, &ignored);
	}
}

bool UndoContentChanges(const fs::path& gameDir, const ContentChanges& changes, std::string* err)
{
	for (const auto& [path, change] : changes) {
		fs::path folder;
		std::string fileName;
		if (!SplitContentPath(gameDir, path, &folder, &fileName))
			continue;
		std::error_code ec;
		fs::path on = folder / fs::u8path(fileName), off = on;
		off += kDisabledSuffix;
		if (!fs::exists(on, ec) && !fs::exists(off, ec))
			continue; // gone already (or removed: Repair files brings those back)
		bool ok = true;
		if (change == "added")
			ok = DeleteContent(folder, fileName, err);
		else if (change == "disabled")
			ok = SetContentEnabled(folder, fileName, true, err);
		else if (change == "enabled")
			ok = SetContentEnabled(folder, fileName, false, err);
		if (!ok)
			return false;
	}
	return true;
}

// ---- Mod metadata ----

namespace {

class JarReader {
public:
	// Reads entries straight from the file (jars can be large), opened with a wide path on
	// Windows so non-ASCII folder names work.
	explicit JarReader(const fs::path& path)
	{
		std::memset(&m_zip, 0, sizeof(m_zip));
#ifdef _WIN32
		m_file = _wfopen(path.c_str(), L"rb");
#else
		m_file = std::fopen(path.c_str(), "rb");
#endif
		m_open = m_file && mz_zip_reader_init_cfile(&m_zip, m_file, 0, 0);
	}
	~JarReader()
	{
		if (m_open)
			mz_zip_reader_end(&m_zip);
		if (m_file)
			std::fclose(m_file);
	}
	JarReader(const JarReader&) = delete;
	JarReader& operator=(const JarReader&) = delete;
	bool Open() const { return m_open; }
	bool Read(const char* name, std::string& out)
	{
		if (!m_open)
			return false;
		const int index = mz_zip_reader_locate_file(&m_zip, name, nullptr, 0);
		if (index < 0)
			return false;
		std::size_t size = 0;
		void* data = mz_zip_reader_extract_to_heap(&m_zip, static_cast<mz_uint>(index), &size, 0);
		if (!data)
			return false;
		out.assign(static_cast<const char*>(data), size);
		mz_free(data);
		return true;
	}

private:
	std::FILE* m_file = nullptr;
	mz_zip_archive m_zip;
	bool m_open = false;
};

std::string JsonString(const json& object, const char* key)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

void ReadAuthors(const json& list, std::vector<std::string>& out)
{
	if (!list.is_array())
		return;
	for (const auto& author : list) {
		if (author.is_string())
			out.push_back(author.get<std::string>());
		else if (author.is_object() && !JsonString(author, "name").empty())
			out.push_back(JsonString(author, "name"));
	}
}

// The value of key in the first [[mods]] table of a mods.toml: "text", 'text' or '''multi-line'''.
std::string TomlModValue(const std::string& toml, const std::string& key)
{
	const std::size_t mods = toml.find("[[mods]]");
	if (mods == std::string::npos)
		return {};
	std::istringstream lines(toml.substr(mods + 8));
	std::string line;
	while (std::getline(lines, line)) {
		const std::size_t start = line.find_first_not_of(" \t");
		if (start == std::string::npos || line[start] == '#')
			continue;
		if (line[start] == '[')
			break; // the next table
		const std::size_t equals = line.find('=', start);
		if (equals == std::string::npos)
			continue;
		std::string name = line.substr(start, equals - start);
		name.erase(name.find_last_not_of(" \t") + 1);
		if (name != key)
			continue;
		std::size_t value = line.find_first_not_of(" \t", equals + 1);
		if (value == std::string::npos)
			return {};
		if (line.compare(value, 3, "'''") == 0 || line.compare(value, 3, "\"\"\"") == 0) {
			const std::string quote = line.substr(value, 3);
			std::string text = line.substr(value + 3);
			std::size_t end = text.find(quote);
			while (end == std::string::npos && std::getline(lines, line)) {
				text += "\n" + line;
				end = text.find(quote);
			}
			return end == std::string::npos ? text : text.substr(0, end);
		}
		const char quote = line[value];
		if (quote == '"' || quote == '\'') {
			const std::size_t end = line.find(quote, value + 1);
			return line.substr(value + 1, end == std::string::npos ? std::string::npos : end - value - 1);
		}
		return {};
	}
	return {};
}

std::string ManifestValue(const std::string& manifest, const std::string& key)
{
	std::istringstream lines(manifest);
	std::string line;
	while (std::getline(lines, line)) {
		if (line.rfind(key + ":", 0) == 0) {
			std::string value = line.substr(key.size() + 1);
			value.erase(0, value.find_first_not_of(' '));
			value.erase(value.find_last_not_of(" \r") + 1);
			return value;
		}
	}
	return {};
}

std::string Trim(std::string text)
{
	text.erase(0, text.find_first_not_of(" \t\r\n"));
	text.erase(text.find_last_not_of(" \t\r\n") + 1);
	return text;
}

}

bool ReadModMetadata(const fs::path& jarPath, ModMetadata& out)
{
	out = {};
	JarReader jar(jarPath);
	if (!jar.Open())
		return false;
	std::string text;
	try {
		if (jar.Read("fabric.mod.json", text)) {
			const json j = json::parse(text, nullptr, true, true);
			out.loader = "Fabric";
			out.id = JsonString(j, "id");
			out.name = JsonString(j, "name");
			out.version = JsonString(j, "version");
			out.description = JsonString(j, "description");
			ReadAuthors(j.value("authors", json()), out.authors);
		} else if (jar.Read("quilt.mod.json", text)) {
			const json loader = json::parse(text, nullptr, true, true).value("quilt_loader", json::object());
			const json metadata = loader.value("metadata", json::object());
			out.loader = "Quilt";
			out.id = JsonString(loader, "id");
			out.version = JsonString(loader, "version");
			out.name = JsonString(metadata, "name");
			out.description = JsonString(metadata, "description");
			const json contributors = metadata.value("contributors", json::object());
			if (contributors.is_object()) {
				for (const auto& [name, role] : contributors.items())
					out.authors.push_back(name);
			}
		} else if (jar.Read("META-INF/neoforge.mods.toml", text) || jar.Read("META-INF/mods.toml", text)) {
			out.loader = text.find("neoforge") != std::string::npos ? "NeoForge" : "Forge";
			out.id = TomlModValue(text, "modId");
			out.name = TomlModValue(text, "displayName");
			out.version = TomlModValue(text, "version");
			out.description = Trim(TomlModValue(text, "description"));
			const std::string authors = TomlModValue(text, "authors");
			if (!authors.empty())
				out.authors.push_back(authors);
			if (out.version.find("${") != std::string::npos) {
				std::string manifest;
				out.version = jar.Read("META-INF/MANIFEST.MF", manifest) ? ManifestValue(manifest, "Implementation-Version") : "";
			}
		} else if (jar.Read("mcmod.info", text)) {
			json j = json::parse(text, nullptr, true, true);
			if (j.is_object())
				j = j.value("modList", json::array());
			if (!j.is_array() || j.empty())
				return false;
			const json& mod = j.front();
			out.loader = "Forge";
			out.id = JsonString(mod, "modid");
			out.name = JsonString(mod, "name");
			out.version = JsonString(mod, "version");
			out.description = JsonString(mod, "description");
			ReadAuthors(mod.value("authorList", json()), out.authors);
		} else {
			return false;
		}
	}
	catch (const std::exception&) {
		return false;
	}
	out.description = Trim(out.description);
	return true;
}

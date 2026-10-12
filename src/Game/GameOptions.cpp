#include "GameOptions.h"
#include "Util/TracedFs.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

static GameOptionDef Toggle(const char* key, const char* group, const char* label, const char* hint, bool defaultOn,
	bool inverted = false)
{
	GameOptionDef def;
	def.key = key;
	def.group = group;
	def.label = label;
	def.hint = hint;
	def.type = GameOptionType::Toggle;
	def.defaultValue = (defaultOn != inverted) ? "true" : "false";
	def.inverted = inverted;
	return def;
}

static GameOptionDef Slider(const char* key, const char* group, const char* label, const char* hint, const char* defaultValue,
	float min, float max, const char* format, float displayScale = 1.0f, float displayOffset = 0.0f, bool integer = false)
{
	GameOptionDef def;
	def.key = key;
	def.group = group;
	def.label = label;
	def.hint = hint;
	def.type = GameOptionType::Slider;
	def.defaultValue = defaultValue;
	def.min = min;
	def.max = max;
	def.format = format;
	def.displayScale = displayScale;
	def.displayOffset = displayOffset;
	def.integer = integer;
	return def;
}

static GameOptionDef IntSlider(const char* key, const char* group, const char* label, const char* hint, const char* defaultValue,
	float min, float max, const char* format)
{
	return Slider(key, group, label, hint, defaultValue, min, max, format, 1.0f, 0.0f, true);
}

// Volumes and other 0-1 values shown as 0-100%.
static GameOptionDef Percent(const char* key, const char* group, const char* label, const char* hint, const char* defaultValue,
	float maxPercent = 100.0f)
{
	return Slider(key, group, label, hint, defaultValue, 0.0f, maxPercent, "%.0f%%", 100.0f);
}

static GameOptionDef Choice(const char* key, const char* group, const char* label, const char* hint, const char* defaultValue,
	std::vector<GameOptionChoice> choices)
{
	GameOptionDef def;
	def.key = key;
	def.group = group;
	def.label = label;
	def.hint = hint;
	def.type = GameOptionType::Choice;
	def.defaultValue = defaultValue;
	def.choices = std::move(choices);
	return def;
}

static GameOptionDef Text(const char* key, const char* group, const char* label, const char* hint, const char* defaultValue)
{
	GameOptionDef def;
	def.key = key;
	def.group = group;
	def.label = label;
	def.hint = hint;
	def.type = GameOptionType::Text;
	def.defaultValue = defaultValue;
	return def;
}

const std::vector<GameOptionDef>& GameOptionDefs()
{
	static const std::vector<GameOptionDef> defs = {
		// Video
		IntSlider("renderDistance", "Video", "Render distance", "Chunks drawn around you.", "12", 2.0f, 32.0f, "%.0f chunks"),
		IntSlider("simulationDistance", "Video", "Simulation distance", "Chunks where mobs and crops update.", "12", 5.0f, 32.0f, "%.0f chunks"),
		IntSlider("maxFps", "Video", "Max framerate", "260 means unlimited.", "120", 10.0f, 260.0f, "%.0f fps"),
		Toggle("enableVsync", "Video", "VSync", nullptr, true),
		Choice("graphicsMode", "Video", "Graphics", nullptr, "1", { { "0", "Fast" }, { "1", "Fancy" }, { "2", "Fabulous!" } }),
		Choice("guiScale", "Video", "GUI scale", "Size of Minecraft's menus and HUD.", "0",
			{ { "0", "Auto" }, { "1", "1" }, { "2", "2" }, { "3", "3" }, { "4", "4" }, { "5", "5" }, { "6", "6" } }),
		Slider("fov", "Video", "Field of view", "Stored by Minecraft as (degrees - 70) / 40.", "0.0", 30.0f, 110.0f, "%.0f\xC2\xB0", 40.0f, 70.0f, true),
		Percent("gamma", "Video", "Brightness", "0% is Moody, 100% is Bright.", "0.5"),
		Choice("particles", "Video", "Particles", nullptr, "0", { { "0", "All" }, { "1", "Decreased" }, { "2", "Minimal" } }),
		Choice("renderClouds", "Video", "Clouds", nullptr, "\"true\"",
			{ { "\"true\"", "Fancy" }, { "\"fast\"", "Fast" }, { "\"false\"", "Off" } }),
		Toggle("ao", "Video", "Smooth lighting", nullptr, true),
		Toggle("entityShadows", "Video", "Entity shadows", nullptr, true),
		Toggle("bobView", "Video", "View bobbing", nullptr, true),
		IntSlider("biomeBlendRadius", "Video", "Biome blend", "Smooths grass and water colors between biomes.", "2", 0.0f, 7.0f, "%.0f"),
		IntSlider("mipmapLevels", "Video", "Mipmap levels", nullptr, "4", 0.0f, 4.0f, "%.0f"),
		Percent("fovEffectScale", "Video", "FOV effects", "How much speed and potions change the field of view.", "1.0"),
		Percent("screenEffectScale", "Video", "Distortion effects", "Nausea and Nether portal wobble.", "1.0"),

		// Controls
		Slider("mouseSensitivity", "Controls", "Mouse sensitivity", nullptr, "0.5", 0.0f, 200.0f, "%.0f%%", 200.0f),
		Toggle("invertYMouse", "Controls", "Invert mouse", nullptr, false),
		Toggle("rawMouseInput", "Controls", "Raw input", "Ignore Windows mouse acceleration.", true),
		Toggle("autoJump", "Controls", "Auto-jump", nullptr, false),
		Toggle("toggleCrouch", "Controls", "Toggle sneak", "Press once instead of holding.", false),
		Toggle("toggleSprint", "Controls", "Toggle sprint", "Press once instead of holding.", false),
		Toggle("pauseOnLostFocus", "Controls", "Pause when unfocused", "Open the pause menu when you alt-tab.", true),

		// Sound
		Percent("soundCategory_master", "Sound", "Master volume", nullptr, "1.0"),
		Percent("soundCategory_music", "Sound", "Music", nullptr, "1.0"),
		Percent("soundCategory_record", "Sound", "Jukebox / note blocks", nullptr, "1.0"),
		Percent("soundCategory_weather", "Sound", "Weather", nullptr, "1.0"),
		Percent("soundCategory_block", "Sound", "Blocks", nullptr, "1.0"),
		Percent("soundCategory_hostile", "Sound", "Hostile creatures", nullptr, "1.0"),
		Percent("soundCategory_neutral", "Sound", "Friendly creatures", nullptr, "1.0"),
		Percent("soundCategory_player", "Sound", "Players", nullptr, "1.0"),
		Percent("soundCategory_ambient", "Sound", "Ambient / environment", nullptr, "1.0"),
		Percent("soundCategory_voice", "Sound", "Voice / speech", nullptr, "1.0"),
		Toggle("showSubtitles", "Sound", "Show subtitles", nullptr, false),

		// Chat & accessibility
		Text("lang", "Chat & accessibility", "Language", "A Minecraft language code, e.g. en_us, de_de, fr_fr.", "en_us"),
		Percent("chatScale", "Chat & accessibility", "Chat text size", nullptr, "1.0"),
		Percent("chatOpacity", "Chat & accessibility", "Chat text opacity", nullptr, "1.0"),
		Choice("narrator", "Chat & accessibility", "Narrator", nullptr, "0",
			{ { "0", "Off" }, { "1", "All" }, { "2", "Chat" }, { "3", "System" } }),
		Toggle("darkMojangStudiosBackground", "Chat & accessibility", "Monochrome logo", "Black-and-white loading screen.", false),
		Toggle("hideLightningFlashes", "Chat & accessibility", "Hide lightning flashes", nullptr, false),

		// First launch: skip the screens new players see.
		Toggle("onboardAccessibility", "First launch", "Skip the accessibility intro", "The screen shown on a pack's first start.", false, true),
		Choice("tutorialStep", "First launch", "Tutorial hints", "The movement and crafting popups for new players.", "movement",
			{ { "movement", "Show" }, { "none", "Skip" } }),
		Toggle("skipMultiplayerWarning", "First launch", "Skip the online play warning", nullptr, false),
		Toggle("joinedFirstServer", "First launch", "Skip the multiplayer intro", "The screen shown before your first server list.", false),
	};
	return defs;
}

const GameOptionDef* FindGameOption(const std::string& key)
{
	for (const GameOptionDef& def : GameOptionDefs()) {
		if (key == def.key)
			return &def;
	}
	return nullptr;
}

float GameOptionToDisplay(const GameOptionDef& def, const std::string& fileValue)
{
	char* end = nullptr;
	const double value = std::strtod(fileValue.c_str(), &end);
	if (end == fileValue.c_str())
		return GameOptionToDisplay(def, def.defaultValue);
	return static_cast<float>(value * def.displayScale + def.displayOffset);
}

std::string GameOptionFromDisplay(const GameOptionDef& def, float display)
{
	if (def.integer)
		display = std::round(display);
	display = std::fmax(def.min, std::fmin(def.max, display));
	const double value = (display - def.displayOffset) / def.displayScale;
	char text[32];
	if (def.integer && def.displayScale == 1.0f)
		std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::lround(value)));
	else
		std::snprintf(text, sizeof(text), "%.4g", value);
	std::string out = text;
	// Minecraft writes floats with a decimal point (e.g. 1.0, not 1).
	if (!(def.integer && def.displayScale == 1.0f) && out.find_first_of(".e") == std::string::npos)
		out += ".0";
	return out;
}

bool ApplyGameOptions(const fs::path& instanceDir, const GameOptionsSettings& options, std::string* err)
{
	if (options.values.empty())
		return true;
	std::error_code ec;
	const fs::path marker = instanceDir / ".mclp-options-applied";
	if (!options.everyLaunch && fs::exists(marker, ec))
		return true;

	// Keep every line, replacing the keys we set and appending the ones that are missing.
	const fs::path path = instanceDir / "options.txt";
	std::vector<std::string> lines;
	{
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			lines.push_back(line);
		}
	}
	std::map<std::string, bool> written;
	for (std::string& line : lines) {
		const std::size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		const auto it = options.values.find(line.substr(0, colon));
		if (it == options.values.end())
			continue;
		line = it->first + ":" + it->second;
		written[it->first] = true;
	}
	for (const auto& [key, value] : options.values) {
		if (!written.count(key))
			lines.push_back(key + ":" + value);
	}

	TracedFs::CreateDirectories(instanceDir, ec);
	const fs::path temp = instanceDir / "options.txt.mclp-tmp";
	{
		auto out = TracedFs::OpenWrite(temp, std::ios::trunc | std::ios::binary);
		for (const std::string& line : lines)
			out << line << "\n";
		if (!out.good()) {
			if (err) *err = "Couldn't write " + path.string();
			return false;
		}
	}
	TracedFs::Rename(temp, path, ec);
	if (ec) {
		if (err) *err = "Couldn't update " + path.string() + ": " + ec.message();
		TracedFs::Remove(temp, ec);
		return false;
	}
	TracedFs::OpenWrite(marker) << "applied\n";
	return true;
}

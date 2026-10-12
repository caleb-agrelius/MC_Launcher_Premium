#pragma once

#include "Storage/Settings.h"

#include <filesystem>
#include <string>
#include <vector>

// Vanilla Minecraft options (options.txt) that Settings > Game options can set for
// every pack. Keys and value formats follow recent Minecraft versions (1.19+);
// versions that don't know a key ignore it.

enum class GameOptionType {
	Toggle, // "true"/"false"
	Slider, // number; shown in display units, see displayScale/displayOffset
	Choice, // one of choices[].value, written as-is
	Text,   // free text, e.g. a language code
};

struct GameOptionChoice {
	const char* value; // as written to options.txt
	const char* label;
};

struct GameOptionDef {
	const char* key;   // options.txt key
	const char* group; // "Video", "Controls", "Sound", "Chat & accessibility", "First launch"
	const char* label;
	const char* hint;  // may be null
	GameOptionType type = GameOptionType::Toggle;
	const char* defaultValue = "false"; // vanilla default, as written to options.txt
	// Slider: shown value = file value * displayScale + displayOffset.
	float min = 0.0f;  // display units
	float max = 1.0f;
	float displayScale = 1.0f;
	float displayOffset = 0.0f;
	bool integer = false;
	const char* format = "%.0f";
	bool inverted = false; // Toggle shown on when the file value is "false"
	std::vector<GameOptionChoice> choices;
};

const std::vector<GameOptionDef>& GameOptionDefs();

// Slider helpers between options.txt values and display units.
float GameOptionToDisplay(const GameOptionDef& def, const std::string& fileValue);
std::string GameOptionFromDisplay(const GameOptionDef& def, float display);

// Writes the chosen options into instanceDir/options.txt, keeping every other line.
// With everyLaunch off, it only does so the first time for each instance (tracked by
// instanceDir/.mclp-options-applied). Does nothing when no options are set.
bool ApplyGameOptions(const std::filesystem::path& instanceDir, const GameOptionsSettings& options, std::string* err);

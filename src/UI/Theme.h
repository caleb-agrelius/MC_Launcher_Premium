#pragma once

#include "imgui.h"

#include <cstddef>
#include <string>

// Launcher look: color themes, Segoe UI text, and sizes that follow the monitor's
// DPI scale times the user's UI scale.
namespace Theme {

// A theme's colors. ImGui's style colors (hover/active shades etc.) are derived from these.
struct Palette {
	ImVec4 windowBg;   // outermost background
	ImVec4 panelBg;    // sidebar, pack list, details pane
	ImVec4 cardBg;     // cards inside panels
	ImVec4 frameBg;    // text boxes, sliders
	ImVec4 border;
	ImVec4 text;
	ImVec4 textDim;    // secondary text
	ImVec4 accent;     // highlights: selection bars, progress, chips, accent text
	ImVec4 accentDeep; // filled accent: buttons, selected rows
	ImVec4 error;
	ImVec4 info;       // secondary tag color (e.g. loader chips)
	bool light = false;
};

struct ThemeInfo {
	const char* id;   // saved in settings.json
	const char* name;
	const char* description;
	Palette palette;
};

// The built-in themes. The first is the default.
const ThemeInfo* Themes();
std::size_t ThemeCount();
// Index of the theme with this id, or 0 (the default) if there's none.
std::size_t FindTheme(const std::string& id);

// The active theme's colors.
const Palette& C();

// Unscaled font sizes, for ImGui::PushFont(font, size). ImGui applies the global scale.
constexpr float kFontBody = 16.0f;
constexpr float kFontSmall = 13.5f;
constexpr float kFontHeading = 19.0f;
constexpr float kFontTitle = 30.0f;
constexpr float kFontHero = 34.0f;

struct Fonts {
	ImFont* regular = nullptr;
	ImFont* bold = nullptr;
};

// Loads Segoe UI (regular + semibold) when available, else ImGui's scalable default font.
// Call once after ImGui::CreateContext().
void LoadFonts();
const Fonts& GetFonts();

// Rebuilds the style for the given theme, monitor DPI scale and user UI scale.
// Call between frames (before ImGui::NewFrame) whenever any of them changes.
void Apply(std::size_t themeIndex, float dpiScale, float userScale);

// Total scale (DPI x user) and a helper to scale design-time pixel sizes.
float Scale();
inline float S(float px) { return px * Scale(); }

// Linear blend between two colors (t = 0 gives a, 1 gives b).
ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t);
ImVec4 WithAlpha(const ImVec4& color, float alpha);

}

#pragma once

#include "imgui.h"

// Launcher look: dark slate panels with green accents, Segoe UI text,
// and sizes that follow the monitor's DPI scale times the user's UI scale.
namespace Theme {

// Palette
inline const ImVec4 kAccent = ImVec4(0.40f, 0.83f, 0.50f, 1.0f);
inline const ImVec4 kAccentDeep = ImVec4(0.16f, 0.34f, 0.22f, 1.0f);
inline const ImVec4 kError = ImVec4(1.0f, 0.42f, 0.38f, 1.0f);
inline const ImVec4 kText = ImVec4(0.92f, 0.95f, 0.93f, 1.0f);
inline const ImVec4 kTextDim = ImVec4(0.55f, 0.62f, 0.60f, 1.0f);
inline const ImVec4 kWindowBg = ImVec4(0.055f, 0.070f, 0.085f, 1.0f);
inline const ImVec4 kPanelBg = ImVec4(0.075f, 0.090f, 0.105f, 1.0f);
inline const ImVec4 kCardBg = ImVec4(0.095f, 0.115f, 0.130f, 1.0f);
inline const ImVec4 kBorder = ImVec4(0.15f, 0.19f, 0.21f, 1.0f);

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

// Rebuilds the style for the given monitor DPI scale and user UI scale.
// Call between frames (before ImGui::NewFrame) whenever either changes.
void Apply(float dpiScale, float userScale);

// Total scale (DPI x user) and a helper to scale design-time pixel sizes.
float Scale();
inline float S(float px) { return px * Scale(); }

}

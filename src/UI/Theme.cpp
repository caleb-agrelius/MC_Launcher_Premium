#include "Theme.h"

#include <filesystem>

namespace Theme {

static Fonts g_fonts;
static float g_scale = 1.0f;

static ImFont* AddFontIfExists(const char* path, const ImFontConfig* config = nullptr)
{
	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
		return nullptr;
	return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, kFontBody, config);
}

// Merges fallback fonts into the most recently added font, so text in other scripts
// (pack descriptions are often Chinese, Russian, ...) and symbols render instead of '?'.
// Glyphs are rasterized on demand, so this costs nothing until they're used.
static void AddFallbackFonts()
{
#ifdef _WIN32
	ImFontConfig merge;
	merge.MergeMode = true;
	const char* fallbacks[] = {
		"C:\\Windows\\Fonts\\msyh.ttc",     // Chinese (Microsoft YaHei)
		"C:\\Windows\\Fonts\\YuGothM.ttc",  // Japanese
		"C:\\Windows\\Fonts\\malgun.ttf",   // Korean
		"C:\\Windows\\Fonts\\seguisym.ttf", // symbols, arrows
		"C:\\Windows\\Fonts\\seguiemj.ttf", // emoji (drawn in one color)
	};
	for (const char* path : fallbacks)
		AddFontIfExists(path, &merge);
#endif
}

void LoadFonts()
{
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();
#ifdef _WIN32
	g_fonts.regular = AddFontIfExists("C:\\Windows\\Fonts\\segoeui.ttf");
	if (g_fonts.regular)
		AddFallbackFonts();
	g_fonts.bold = AddFontIfExists("C:\\Windows\\Fonts\\seguisb.ttf");
	if (!g_fonts.bold)
		g_fonts.bold = AddFontIfExists("C:\\Windows\\Fonts\\segoeuib.ttf");
	if (g_fonts.bold)
		AddFallbackFonts();
#endif
	if (!g_fonts.regular)
		g_fonts.regular = io.Fonts->AddFontDefaultVector();
	if (!g_fonts.bold)
		g_fonts.bold = g_fonts.regular;
	io.FontDefault = g_fonts.regular;
}

const Fonts& GetFonts()
{
	return g_fonts;
}

float Scale()
{
	return g_scale;
}

void Apply(float dpiScale, float userScale)
{
	g_scale = dpiScale * userScale;

	ImGuiStyle& style = ImGui::GetStyle();
	style = ImGuiStyle();
	ImGui::StyleColorsDark(&style);
	style.WindowRounding = 10.0f;
	style.ChildRounding = 10.0f;
	style.FrameRounding = 6.0f;
	style.PopupRounding = 6.0f;
	style.ScrollbarRounding = 8.0f;
	style.ScrollbarSize = 12.0f;
	style.GrabRounding = 6.0f;
	style.WindowPadding = ImVec2(18.0f, 16.0f);
	style.FramePadding = ImVec2(12.0f, 8.0f);
	style.ItemSpacing = ImVec2(10.0f, 8.0f);
	style.ChildBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = kText;
	colors[ImGuiCol_TextDisabled] = kTextDim;
	colors[ImGuiCol_WindowBg] = kWindowBg;
	colors[ImGuiCol_ChildBg] = kPanelBg;
	colors[ImGuiCol_PopupBg] = kPanelBg;
	colors[ImGuiCol_Border] = kBorder;
	colors[ImGuiCol_Separator] = kBorder;
	colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.13f, 0.14f, 1.0f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.20f, 0.17f, 1.0f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.25f, 0.19f, 1.0f);
	colors[ImGuiCol_Button] = kAccentDeep;
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.43f, 0.28f, 1.0f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.13f, 0.29f, 0.19f, 1.0f);
	colors[ImGuiCol_Header] = ImVec4(0.13f, 0.25f, 0.18f, 1.0f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.12f, 0.19f, 0.16f, 1.0f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.38f, 0.25f, 1.0f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.38f, 0.80f, 0.49f, 1.0f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.68f, 0.40f, 1.0f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.82f, 0.50f, 1.0f);
	colors[ImGuiCol_PlotHistogram] = kAccent;
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.18f, 0.23f, 0.24f, 1.0f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.22f, 0.32f, 0.27f, 1.0f);
	colors[ImGuiCol_ScrollbarGrabActive] = kAccentDeep;
	colors[ImGuiCol_NavCursor] = kAccent;

	style.ScaleAllSizes(g_scale);
	style.FontSizeBase = kFontBody;
	style.FontScaleDpi = dpiScale;
	style.FontScaleMain = userScale;
}

}

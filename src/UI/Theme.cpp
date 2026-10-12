#include "Theme.h"

#include <filesystem>
#include <iterator>

namespace Theme {

static constexpr ImVec4 RGB(float r, float g, float b) { return ImVec4(r, g, b, 1.0f); }

// Each theme is named after a Minecraft place or block. Grass Block is the original look.
static const ThemeInfo kThemes[] = {
	{ "grass", "Grass Block", "The original slate and green.", {
		RGB(0.055f, 0.070f, 0.085f), RGB(0.075f, 0.090f, 0.105f), RGB(0.095f, 0.115f, 0.130f),
		RGB(0.10f, 0.13f, 0.14f), RGB(0.15f, 0.19f, 0.21f),
		RGB(0.92f, 0.95f, 0.93f), RGB(0.55f, 0.62f, 0.60f),
		RGB(0.40f, 0.83f, 0.50f), RGB(0.16f, 0.34f, 0.22f),
		RGB(1.0f, 0.42f, 0.38f), RGB(0.45f, 0.65f, 0.95f), false } },
	{ "deep_dark", "Deep Dark", "Near-black with glowing sculk cyan.", {
		RGB(0.027f, 0.040f, 0.050f), RGB(0.040f, 0.058f, 0.070f), RGB(0.055f, 0.078f, 0.092f),
		RGB(0.06f, 0.09f, 0.105f), RGB(0.09f, 0.15f, 0.17f),
		RGB(0.86f, 0.95f, 0.96f), RGB(0.45f, 0.58f, 0.62f),
		RGB(0.16f, 0.86f, 0.88f), RGB(0.05f, 0.30f, 0.34f),
		RGB(1.0f, 0.42f, 0.42f), RGB(0.55f, 0.60f, 1.0f), false } },
	{ "nether", "Nether", "Netherrack red and lava orange.", {
		RGB(0.075f, 0.035f, 0.035f), RGB(0.100f, 0.048f, 0.045f), RGB(0.125f, 0.060f, 0.056f),
		RGB(0.15f, 0.07f, 0.065f), RGB(0.25f, 0.11f, 0.10f),
		RGB(0.97f, 0.91f, 0.88f), RGB(0.68f, 0.52f, 0.48f),
		RGB(1.0f, 0.48f, 0.22f), RGB(0.45f, 0.13f, 0.08f),
		RGB(1.0f, 0.35f, 0.35f), RGB(1.0f, 0.78f, 0.35f), false } },
	{ "the_end", "The End", "Void purple with end stone highlights.", {
		RGB(0.045f, 0.035f, 0.070f), RGB(0.065f, 0.050f, 0.095f), RGB(0.085f, 0.066f, 0.120f),
		RGB(0.10f, 0.08f, 0.14f), RGB(0.18f, 0.14f, 0.26f),
		RGB(0.94f, 0.92f, 0.98f), RGB(0.60f, 0.56f, 0.70f),
		RGB(0.78f, 0.55f, 1.0f), RGB(0.30f, 0.18f, 0.46f),
		RGB(1.0f, 0.45f, 0.50f), RGB(0.95f, 0.92f, 0.65f), false } },
	{ "prismarine", "Prismarine", "Ocean monument teal.", {
		RGB(0.035f, 0.070f, 0.075f), RGB(0.050f, 0.092f, 0.098f), RGB(0.065f, 0.115f, 0.120f),
		RGB(0.07f, 0.13f, 0.135f), RGB(0.12f, 0.22f, 0.22f),
		RGB(0.90f, 0.97f, 0.95f), RGB(0.52f, 0.66f, 0.64f),
		RGB(0.38f, 0.85f, 0.75f), RGB(0.12f, 0.36f, 0.33f),
		RGB(1.0f, 0.45f, 0.42f), RGB(0.55f, 0.75f, 1.0f), false } },
	{ "cherry", "Cherry Grove", "Soft blossom pink.", {
		RGB(0.075f, 0.050f, 0.065f), RGB(0.100f, 0.068f, 0.088f), RGB(0.125f, 0.086f, 0.110f),
		RGB(0.15f, 0.10f, 0.13f), RGB(0.26f, 0.17f, 0.22f),
		RGB(0.98f, 0.93f, 0.96f), RGB(0.70f, 0.58f, 0.65f),
		RGB(1.0f, 0.62f, 0.80f), RGB(0.48f, 0.22f, 0.36f),
		RGB(1.0f, 0.45f, 0.40f), RGB(0.65f, 0.85f, 0.55f), false } },
	{ "sandstone", "Sandstone", "Warm desert gold.", {
		RGB(0.075f, 0.065f, 0.048f), RGB(0.100f, 0.088f, 0.066f), RGB(0.125f, 0.110f, 0.084f),
		RGB(0.15f, 0.13f, 0.10f), RGB(0.25f, 0.22f, 0.16f),
		RGB(0.97f, 0.94f, 0.86f), RGB(0.70f, 0.64f, 0.52f),
		RGB(0.95f, 0.78f, 0.40f), RGB(0.42f, 0.32f, 0.14f),
		RGB(1.0f, 0.45f, 0.38f), RGB(0.55f, 0.75f, 0.95f), false } },
	{ "redstone", "Redstone", "Stone grey wired with red.", {
		RGB(0.060f, 0.060f, 0.064f), RGB(0.082f, 0.082f, 0.088f), RGB(0.105f, 0.105f, 0.112f),
		RGB(0.12f, 0.12f, 0.13f), RGB(0.20f, 0.20f, 0.22f),
		RGB(0.94f, 0.94f, 0.95f), RGB(0.60f, 0.60f, 0.63f),
		RGB(1.0f, 0.28f, 0.25f), RGB(0.46f, 0.10f, 0.10f),
		RGB(1.0f, 0.60f, 0.30f), RGB(0.95f, 0.80f, 0.40f), false } },
	{ "diamond", "Diamond", "Midnight blue with icy cyan.", {
		RGB(0.040f, 0.055f, 0.085f), RGB(0.055f, 0.075f, 0.110f), RGB(0.072f, 0.095f, 0.135f),
		RGB(0.08f, 0.11f, 0.16f), RGB(0.13f, 0.18f, 0.27f),
		RGB(0.92f, 0.96f, 1.0f), RGB(0.55f, 0.63f, 0.75f),
		RGB(0.45f, 0.90f, 0.95f), RGB(0.14f, 0.32f, 0.45f),
		RGB(1.0f, 0.45f, 0.45f), RGB(0.70f, 0.60f, 1.0f), false } },
	{ "birch", "Birch", "A light theme in pale birch wood.", {
		RGB(0.90f, 0.89f, 0.85f), RGB(0.96f, 0.95f, 0.92f), RGB(0.995f, 0.99f, 0.975f),
		RGB(0.88f, 0.87f, 0.82f), RGB(0.78f, 0.76f, 0.70f),
		RGB(0.13f, 0.15f, 0.13f), RGB(0.42f, 0.44f, 0.40f),
		RGB(0.20f, 0.56f, 0.30f), RGB(0.70f, 0.85f, 0.70f),
		RGB(0.80f, 0.20f, 0.18f), RGB(0.20f, 0.40f, 0.80f), true } },
};

static Fonts g_fonts;
static float g_scale = 1.0f;
static std::size_t g_theme = 0;

const ThemeInfo* Themes()
{
	return kThemes;
}

std::size_t ThemeCount()
{
	return std::size(kThemes);
}

std::size_t FindTheme(const std::string& id)
{
	for (std::size_t i = 0; i < std::size(kThemes); ++i) {
		if (id == kThemes[i].id)
			return i;
	}
	return 0;
}

const Palette& C()
{
	return kThemes[g_theme].palette;
}

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
	return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 WithAlpha(const ImVec4& color, float alpha)
{
	return ImVec4(color.x, color.y, color.z, color.w * alpha);
}

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

void Apply(std::size_t themeIndex, float dpiScale, float userScale)
{
	g_theme = themeIndex < std::size(kThemes) ? themeIndex : 0;
	g_scale = dpiScale * userScale;
	const Palette& p = C();

	ImGuiStyle& style = ImGui::GetStyle();
	style = ImGuiStyle();
	if (p.light)
		ImGui::StyleColorsLight(&style);
	else
		ImGui::StyleColorsDark(&style);
	style.WindowRounding = 10.0f;
	style.ChildRounding = 10.0f;
	style.FrameRounding = 6.0f;
	style.PopupRounding = 6.0f;
	style.ScrollbarRounding = 8.0f;
	style.ScrollbarSize = 12.0f;
	style.GrabRounding = 6.0f;
	style.GrabMinSize = 14.0f;
	style.WindowPadding = ImVec2(18.0f, 16.0f);
	style.FramePadding = ImVec2(12.0f, 8.0f);
	style.ItemSpacing = ImVec2(10.0f, 8.0f);
	style.ChildBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = p.text;
	colors[ImGuiCol_TextDisabled] = p.textDim;
	colors[ImGuiCol_WindowBg] = p.windowBg;
	colors[ImGuiCol_ChildBg] = p.panelBg;
	colors[ImGuiCol_PopupBg] = p.cardBg;
	colors[ImGuiCol_Border] = p.border;
	colors[ImGuiCol_Separator] = p.border;
	colors[ImGuiCol_FrameBg] = p.frameBg;
	colors[ImGuiCol_FrameBgHovered] = Mix(p.frameBg, p.accentDeep, 0.35f);
	colors[ImGuiCol_FrameBgActive] = Mix(p.frameBg, p.accentDeep, 0.55f);
	colors[ImGuiCol_Button] = p.accentDeep;
	colors[ImGuiCol_ButtonHovered] = Mix(p.accentDeep, p.accent, 0.25f);
	colors[ImGuiCol_ButtonActive] = Mix(p.accentDeep, p.windowBg, 0.25f);
	colors[ImGuiCol_Header] = p.accentDeep;
	colors[ImGuiCol_HeaderHovered] = Mix(p.panelBg, p.accentDeep, 0.5f);
	colors[ImGuiCol_HeaderActive] = Mix(p.accentDeep, p.accent, 0.2f);
	colors[ImGuiCol_CheckMark] = p.accent;
	colors[ImGuiCol_SliderGrab] = Mix(p.accentDeep, p.accent, 0.6f);
	colors[ImGuiCol_SliderGrabActive] = p.accent;
	colors[ImGuiCol_PlotHistogram] = p.accent;
	colors[ImGuiCol_TextSelectedBg] = WithAlpha(p.accent, 0.35f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	colors[ImGuiCol_ScrollbarGrab] = Mix(p.border, p.text, 0.08f);
	colors[ImGuiCol_ScrollbarGrabHovered] = Mix(p.border, p.accent, 0.3f);
	colors[ImGuiCol_ScrollbarGrabActive] = p.accentDeep;
	colors[ImGuiCol_NavCursor] = p.accent;

	style.ScaleAllSizes(g_scale);
	style.FontSizeBase = kFontBody;
	style.FontScaleDpi = dpiScale;
	style.FontScaleMain = userScale;
}

}

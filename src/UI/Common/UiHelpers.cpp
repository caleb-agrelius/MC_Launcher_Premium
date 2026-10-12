#include "UI/Common/UiHelpers.h"

#include "Model/Modpack.h"
#include "UI/ImageCache.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <algorithm>
#include <string>

namespace UiHelpers {

const char* const kLoaderKeys[4] = { "forge", "neoforge", "fabric", "quilt" };

void AlignRightForButton(const char* label)
{
	const float width = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
	if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x <= ImGui::GetWindowPos().x + x)
		ImGui::SameLine(x);
	else
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x));
}

void AlignRightForButtons(const std::vector<const char*>& labels)
{
	if (labels.empty())
		return;
	const ImGuiStyle& style = ImGui::GetStyle();
	float width = 0.0f;
	for (const char* label : labels)
		width += ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f;
	width += style.ItemSpacing.x * static_cast<float>(labels.size() - 1);
	const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
	if (ImGui::GetItemRectMax().x + style.ItemSpacing.x <= ImGui::GetWindowPos().x + x)
		ImGui::SameLine(x);
	else
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x));
}

void PackIconItem(const Modpack& pack, ImageCache& images, float size)
{
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(size, size));
	Widgets::DrawPackIcon(ImGui::GetWindowDrawList(), images.Get(pack.iconUrl), pack, pos, size, Theme::S(8.0f));
}

void DrawPageHeader(const char* breadcrumb, const char* title, const char* subtitle)
{
	const Theme::Fonts& fonts = Theme::GetFonts();
	Widgets::Text(fonts.bold, Theme::kFontSmall, Theme::C().accent, breadcrumb);
	Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::C().text, title);
	if (subtitle)
		ImGui::TextDisabled("%s", subtitle);
}

ImVec4 SourceColor(PackSource source)
{
	switch (source) {
	case PackSource::Modrinth: return ImVec4(0.11f, 0.85f, 0.42f, 1.0f);
	case PackSource::CurseForge: return ImVec4(0.95f, 0.39f, 0.21f, 1.0f);
	case PackSource::FTB: return ImVec4(0.86f, 0.24f, 0.26f, 1.0f);
	case PackSource::Technic: return ImVec4(0.25f, 0.6f, 0.95f, 1.0f);
	case PackSource::ATLauncher: return ImVec4(0.6f, 0.82f, 0.27f, 1.0f);
	default: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
	}
}

const char* LoaderLabel(const char* loader)
{
	const std::string key = loader;
	if (key == "forge") return "Forge";
	if (key == "neoforge") return "NeoForge";
	if (key == "fabric") return "Fabric";
	if (key == "quilt") return "Quilt";
	return loader;
}

void BusyLabel(const char* text)
{
	Widgets::Spinner(Theme::C().accent);
	ImGui::SameLine();
	ImGui::TextDisabled("%s", text);
}

void EmptyWithRefresh(const char* message, const char* buttonLabel, bool& reload)
{
	ImGui::TextDisabled("%s", message);
	if (ImGui::Button(buttonLabel))
		reload = true;
}

}

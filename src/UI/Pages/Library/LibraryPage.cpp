#include "UI/Pages/Library/LibraryPage.h"

#include "Services/Library/PackLibrary.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Widgets.h"

void LibraryPage::DrawHeader()
{
	UiHelpers::DrawPageHeader("MINECRAFT  /  LIBRARY", "My library", nullptr);
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("Modpacks you installed or made are listed here.");
	UiHelpers::AlignRightForButton("New pack...");
	if (Widgets::PrimaryButton("New pack..."))
		m_dialogs.NewPack();
	ImGui::SetItemTooltip("Make your own pack: pick a Minecraft version and loader, then add mods");
}

void LibraryPage::Draw(const PageFrame& frame)
{
	int cardIndex = 0;
	for (auto& entry : m_library.Packs()) {
		Widgets::BeginEntrance(frame.shownAt, cardIndex++);
		m_card.Draw(entry.second);
		Widgets::EndEntrance();
	}
	if (m_library.Packs().empty() && Widgets::EmptyState("Your library is empty",
			"Packs you download and install from Discover show up here.", "Browse modpacks"))
		m_navigator.ShowPage(Page::Discover);
}

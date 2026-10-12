#include "UI/Pages/Manage/ChangelogTab.h"

#include "Network/PackSources.h"
#include "UI/Common/Background.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include "imgui.h"
#include "imgui_stdlib.h"

void ChangelogTab::Load(const ManagePageContext& context)
{
	const Modpack& pack = *context.pack;
	const std::string key = pack.id + "@" + pack.sourceVersionId + "@" + pack.version;
	if (m_changelogsLoading.count(key) || !context.http)
		return;
	m_changelogsLoading.insert(key);
	m_changelogErrors.erase(key);
	IHttpClient* http = context.http;
	struct Fetched {
		std::string text;
		std::string error;
		bool ok = false;
	};
	RunInBackground(context.tasks, [http, pack]() {
		Fetched fetched;
		fetched.ok = FetchPackChangelog(*http, pack, fetched.text, &fetched.error);
		return fetched;
	}, [this, key](Fetched fetched) {
		m_changelogsLoading.erase(key);
		if (fetched.ok)
			m_changelogs[key] = fetched.text;
		else
			m_changelogErrors[key] = fetched.error;
	});
}

void ChangelogTab::Draw(const ManagePageContext& context, ManageFrame&)
{
	const Modpack& pack = *context.pack;
	if (pack.source == PackSource::Custom) {
		ImGui::TextDisabled("You made this pack, so it has no changelog.");
		return;
	}
	if (!GetSourceCapabilities(pack.source).changelogs) {
		Widgets::WrappedText(Theme::C().textDim, (std::string(PackSourceName(pack.source)) +
			" doesn't publish changelogs. The pack's page may list what changed.").c_str());
		if (!pack.websiteUrl.empty())
			ImGui::TextLinkOpenURL("Open the pack's page", pack.websiteUrl.c_str());
		return;
	}
	const std::string key = pack.id + "@" + pack.sourceVersionId + "@" + pack.version;
	const auto text = m_changelogs.find(key);
	const auto error = m_changelogErrors.find(key);
	if (text == m_changelogs.end() && error == m_changelogErrors.end())
		Load(context);
	Widgets::Text(Theme::GetFonts().bold, Theme::kFontHeading, Theme::C().text, ("What's new in " + pack.version).c_str());
	ImGui::Spacing();
	if (m_changelogsLoading.count(key)) {
		// A card with placeholder lines, so the link below it doesn't jump when the text arrives.
		Widgets::BeginCard("changelog_loading");
		UiHelpers::BusyLabel("Loading the changelog...");
		ImGui::Spacing();
		const float width = ImGui::GetContentRegionAvail().x;
		for (int line = 0; line < 6; ++line)
			Widgets::Skeleton(ImVec2(line % 3 == 2 ? width * 0.55f : width, ImGui::GetTextLineHeightWithSpacing()), 4.0f, Theme::S(10.0f));
		Widgets::EndCard();
	} else if (error != m_changelogErrors.end()) {
		Widgets::WrappedText(Theme::C().error, ("Couldn't load the changelog: " + error->second).c_str());
		if (ImGui::Button("Retry"))
			Load(context);
	} else if (text != m_changelogs.end()) {
		Widgets::BeginCard("changelog_card");
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(text->second.empty() ? "This version has no changelog." : text->second.c_str());
		ImGui::PopTextWrapPos();
		Widgets::EndCard();
	}
	if (!pack.websiteUrl.empty()) {
		ImGui::Spacing();
		ImGui::TextLinkOpenURL("Older versions on the pack's page", pack.websiteUrl.c_str());
	}
}

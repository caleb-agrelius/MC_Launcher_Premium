#include "NewPackDialog.h"

#include "Game/PackCreator.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <memory>

using Theme::S;

namespace {

struct LoaderOption {
	const char* label;
	const char* key; // pack.json's "loader"
};

constexpr LoaderOption kLoaders[] = {
	{ "None (vanilla)", "" },
	{ "Fabric", "fabric" },
	{ "Quilt", "quilt" },
	{ "Forge", "forge" },
	{ "NeoForge", "neoforge" },
};
constexpr int kLoaderCount = static_cast<int>(sizeof(kLoaders) / sizeof(kLoaders[0]));

}

void NewPackDialog::Open()
{
	m_open = true;
	m_name = "My pack";
	m_loaderError.clear();
}

void NewPackDialog::LoadReleases(const NewPackDialogContext& context)
{
	if (m_releasesLoading || !context.tasks.run)
		return;
	m_releasesLoading = true;
	m_releasesError.clear();
	IHttpClient* http = context.http;
	auto post = context.tasks.post;
	context.tasks.run([this, http, post]() {
		auto releases = std::make_shared<std::vector<std::string>>();
		auto error = std::make_shared<std::string>();
		FetchMinecraftReleases(*http, *releases, error.get());
		post([this, releases, error]() {
			m_releasesLoading = false;
			m_releases = *releases;
			m_releasesError = m_releases.empty() ? (error->empty() ? "No Minecraft versions found." : *error) : std::string();
			m_release = 0;
		});
	});
}

void NewPackDialog::LoadLoaderVersions(const NewPackDialogContext& context)
{
	const std::string loader = kLoaders[m_loader].key;
	const std::string minecraft = m_releases[static_cast<std::size_t>(m_release)];
	const std::string key = loader + "@" + minecraft;
	if (m_loaderVersionsFor == key || !context.tasks.run)
		return;
	m_loaderVersionsFor = key;
	m_loaderVersions.clear();
	m_loaderVersion = 0;
	m_loaderError.clear();
	if (loader.empty())
		return; // vanilla
	m_loaderLoading = true;
	IHttpClient* http = context.http;
	auto post = context.tasks.post;
	context.tasks.run([this, http, post, loader, minecraft, key]() {
		auto versions = std::make_shared<std::vector<std::string>>();
		auto error = std::make_shared<std::string>();
		const bool ok = FetchLoaderVersions(*http, loader, minecraft, *versions, error.get());
		post([this, versions, error, ok, key]() {
			if (key != m_loaderVersionsFor)
				return; // the player picked something else meanwhile
			m_loaderLoading = false;
			m_loaderVersions = *versions;
			m_loaderError = !ok ? *error : std::string();
		});
	});
}

bool NewPackDialog::Draw(const NewPackDialogContext& context, NewPackRequest& request)
{
	if (!m_open)
		return false;
	if (m_releases.empty() && !m_releasesLoading && m_releasesError.empty())
		LoadReleases(context);
	if (!m_releases.empty())
		LoadLoaderVersions(context);

	bool create = false;
	ImGui::OpenPopup("New pack");
	ImGui::SetNextWindowSize(ImVec2(std::min(S(560.0f), ImGui::GetIO().DisplaySize.x * 0.9f), 0.0f));
	if (ImGui::BeginPopupModal("New pack", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("Pick a Minecraft version and a mod loader. Then add mods, resource packs and shaders "
			"from Modrinth or your own files on the pack's Content tab.");
		ImGui::PopTextWrapPos();
		ImGui::Spacing();

		const float labelWidth = S(130.0f);
		auto label = [&](const char* text) {
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(text);
			ImGui::SameLine(labelWidth);
			ImGui::SetNextItemWidth(-1.0f);
		};

		label("Name");
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::InputText("##new_pack_name", &m_name);

		label("Minecraft");
		if (m_releasesLoading) {
			Widgets::Spinner(Theme::C().accent);
			ImGui::SameLine();
			ImGui::TextDisabled("Reading Minecraft versions...");
		} else if (!m_releasesError.empty()) {
			ImGui::TextColored(Theme::C().error, "%s", m_releasesError.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Retry"))
				LoadReleases(context);
		} else if (Widgets::BeginSelect("##new_pack_minecraft", m_releases[static_cast<std::size_t>(m_release)].c_str())) {
			for (int i = 0; i < static_cast<int>(m_releases.size()); ++i) {
				ImGui::PushID(i);
				if (Widgets::ListItem(m_releases[static_cast<std::size_t>(i)].c_str(), i == m_release))
					m_release = i;
				ImGui::PopID();
			}
			Widgets::EndSelect();
		}

		label("Mod loader");
		const char* loaderLabels[kLoaderCount];
		for (int i = 0; i < kLoaderCount; ++i)
			loaderLabels[i] = kLoaders[i].label;
		Widgets::Select("##new_pack_loader", &m_loader, loaderLabels, kLoaderCount);

		const bool vanilla = kLoaders[m_loader].key[0] == '\0';
		if (!vanilla) {
			label("Loader version");
			if (m_releases.empty() || m_loaderLoading) {
				Widgets::Spinner(Theme::C().accent);
				ImGui::SameLine();
				ImGui::TextDisabled("Reading %s versions...", kLoaders[m_loader].label);
			} else if (!m_loaderError.empty()) {
				ImGui::TextColored(Theme::C().error, "%s", m_loaderError.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Retry##loader")) {
					m_loaderVersionsFor.clear();
					LoadLoaderVersions(context);
				}
			} else if (m_loaderVersions.empty()) {
				ImGui::TextColored(Theme::C().error, "%s has no version for Minecraft %s.", kLoaders[m_loader].label,
					m_releases[static_cast<std::size_t>(m_release)].c_str());
			} else if (Widgets::BeginSelect("##new_pack_loader_version",
						   m_loaderVersions[static_cast<std::size_t>(m_loaderVersion)].c_str())) {
				for (int i = 0; i < static_cast<int>(m_loaderVersions.size()); ++i) {
					ImGui::PushID(i);
					if (Widgets::ListItem(m_loaderVersions[static_cast<std::size_t>(i)].c_str(), i == m_loaderVersion, true,
							i == 0 ? "newest" : nullptr))
						m_loaderVersion = i;
					ImGui::PopID();
				}
				Widgets::EndSelect();
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		std::string name = m_name;
		name.erase(0, name.find_first_not_of(" \t"));
		name.erase(name.find_last_not_of(" \t") + 1);
		const bool ready = !name.empty() && !m_releases.empty() && (vanilla || (!m_loaderLoading && !m_loaderVersions.empty()));
		ImGui::BeginDisabled(!ready);
		if (Widgets::PrimaryButton("Create pack")) {
			request.name = name;
			request.minecraftVersion = m_releases[static_cast<std::size_t>(m_release)];
			request.loader = kLoaders[m_loader].key;
			request.loaderVersion = vanilla ? std::string() : m_loaderVersions[static_cast<std::size_t>(m_loaderVersion)];
			create = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
			m_open = false;
		if (create)
			m_open = false;
		if (!m_open)
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	return create;
}

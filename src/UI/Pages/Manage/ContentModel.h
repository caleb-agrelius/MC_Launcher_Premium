#pragma once

#include "Game/InstanceContent.h"
#include "UI/Pages/Manage/ManageTypes.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The pack's mods, resource packs and shader packs as read from its game folder, and what the
// Content tab and the Modrinth browser report about working on them. UI thread only.
class ContentModel {
public:
	static constexpr int kKindCount = 3;
	static constexpr ContentKind kKinds[kKindCount] = { ContentKind::Mods, ContentKind::ResourcePacks, ContentKind::ShaderPacks };

	std::vector<ContentFile> lists[kKindCount];
	std::string error;   // shown in red above the list
	std::string notice;  // e.g. "Added sodium.jar."
	// Mod metadata by jar file name, filled by a background scan.
	std::shared_ptr<std::map<std::string, ModMetadata>> metadata;
	bool scanning = false;
	bool reloadRequested = false; // background installs finished; read the folders next frame

	// Reads every folder and starts reading mod names. Forgets everything about the previous pack.
	void Reload(const ManagePageContext& context);
	// Reads only the folders (after adding files).
	void ReloadFolders(const ManagePageContext& context);

	// Copies files into the game folder as mods, resource packs or shader packs; preferredKind is
	// the one the player was looking at. Returns the kind index to show, or -1 when nothing was added.
	int AddFiles(const ManagePageContext& context, const std::vector<std::filesystem::path>& paths, int preferredKind);

	std::size_t Count(int kind) const { return lists[kind].size(); }

	// The error and notice lines, when there are any.
	void DrawMessages() const;

private:
	void ScanMetadata(const ManagePageContext& context);

	int m_scanGeneration = 0; // bumped when the content is re-read, so an older metadata scan is ignored
};

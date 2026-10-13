#pragma once

#include "Core/Runtime.h"
#include "UI/Dialogs/Dialogs.h"
#include "UI/Pages/Manage/ManagePage.h"
#include "UI/Navigation.h"
#include "UI/Pages/IPage.h"

#include <string>

class ImageCache;
class PackContentService;
class PackLibrary;
class PlayService;
class ServerService;
class SettingsStore;
class UpdateService;

// Connects the Manage page (the full-page view of one library pack) to the app: it describes
// the pack and what is happening to it, and carries out what the player asks for.
class ManagePageHost final : public IPackPage {
public:
	ManagePageHost(const Runtime& runtime, PackLibrary& library, PlayService& plays, UpdateService& updates,
		ServerService& servers, PackContentService& content, SettingsStore& settings, ImageCache& images,
		INavigator& navigator, IDialogs& dialogs);

	// Shows this pack; with browse, opened on Content > Browse.
	void SelectPack(const std::string& packId, bool browse) override;

	void DrawHeader() override;
	void Draw(const PageFrame& frame) override;
	bool Available() const override;

private:
	ManagePageContext BuildContext(Modpack& pack, const PageFrame& frame);
	void Handle(Modpack& pack, const ManageRequest& request);

	Runtime m_rt;
	PackLibrary& m_library;
	PlayService& m_plays;
	UpdateService& m_updates;
	ServerService& m_servers;
	PackContentService& m_content;
	SettingsStore& m_settings;
	ImageCache& m_images;
	INavigator& m_navigator;
	IDialogs& m_dialogs;
	ManagePage m_page;
	std::string m_packId;
};

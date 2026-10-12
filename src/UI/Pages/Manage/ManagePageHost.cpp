#include "UI/Pages/Manage/ManagePageHost.h"

#include "Services/Library/PackContentService.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Play/PlayService.h"
#include "Services/Servers/ServerService.h"
#include "Services/Settings/SettingsStore.h"
#include "Services/Updates/UpdateService.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <filesystem>

namespace fs = std::filesystem;

ManagePageHost::ManagePageHost(const Runtime& runtime, PackLibrary& library, PlayService& plays, UpdateService& updates,
	ServerService& servers, PackContentService& content, SettingsStore& settings, ImageCache& images,
	INavigator& navigator, IDialogs& dialogs)
	: m_rt(runtime), m_library(library), m_plays(plays), m_updates(updates), m_servers(servers), m_content(content),
	  m_settings(settings), m_images(images), m_navigator(navigator), m_dialogs(dialogs)
{
}

void ManagePageHost::SelectPack(const std::string& packId, bool browse)
{
	m_packId = packId;
	if (browse)
		m_page.ShowBrowse();
}

bool ManagePageHost::Available() const
{
	return m_library.Contains(m_packId);
}

void ManagePageHost::DrawHeader()
{
	UiHelpers::DrawPageHeader("MINECRAFT  /  LIBRARY  /  MANAGE", m_library.Packs().at(m_packId).name.c_str(), nullptr);
}

ManagePageContext ManagePageHost::BuildContext(Modpack& pack, const PageFrame& frame)
{
	ManagePageContext context;
	context.pack = &pack;
	context.gameDir = fs::absolute(m_library.GameDir(pack));
	context.installDir = fs::absolute(m_library.InstallDir(pack));
	context.http = &m_rt.http;
	context.images = &m_images;
	context.tasks.run = [&tasks = m_rt.tasks](std::function<void()> work) { tasks.Run(std::move(work)); };
	context.tasks.post = [&ui = m_rt.ui](std::function<void()> task) { ui.Post(std::move(task)); };
	const Settings& settings = m_settings.Get();
	context.showGalleryBanners = settings.showGalleryBanners;
	context.hasLaunchSettings = settings.packLaunch.count(pack.id) > 0;
	context.canHostServer = m_servers.CanHost(pack);
	context.shownAt = frame.shownAt;
	// Packs the player made have no pack files to compare with, so they're never "modified".
	const bool customPack = pack.source == PackSource::Custom;
	context.canRepair = !customPack;
	context.canUpdate = !customPack && pack.source != PackSource::LocalIndex;
	if (const PackUpdate* running = m_updates.Running(pack.id)) {
		context.updating = true;
		context.updateStatus = running->copying
			? "Copying your worlds and settings to " + running->toVersion
			: "Updating to " + running->toVersion + " (progress is on the Downloads page)";
	}
	// The player's edits belong to the active version and are saved as they happen.
	InstalledVersion* editing = m_content.EditingVersion(pack);
	context.changes = editing && !customPack ? &editing->contentChanges : nullptr;
	if (!customPack)
		context.recordChange = [this, packId = pack.id, versionKey = editing ? editing->key : std::string()](
			const std::string& path, ContentChange change) {
			m_content.RecordChange(packId, versionKey, path, change);
		};
	if (frame.droppedPaths)
		context.droppedPaths = *frame.droppedPaths;
	const PlayState play = m_plays.StateOf(m_plays.Key(pack));
	context.playBusy = play.busy;
	context.playing = play.playing;
	context.playCancelling = play.busy && play.cancel->load();
	context.playStep = play.step;
	context.playError = play.error;
	context.playProgress = play.progress;
	return context;
}

void ManagePageHost::Handle(Modpack& pack, const ManageRequest& request)
{
	switch (request.action) {
	case ManageAction::Back: m_navigator.ShowPage(Page::Library); break;
	case ManageAction::Play: m_plays.Start(pack); break;
	case ManageAction::CancelPlay: m_plays.Cancel(m_plays.Key(pack)); break;
	case ManageAction::StopGame: m_dialogs.ConfirmStopGame(m_plays.Key(pack)); break;
	case ManageAction::LaunchSettings: m_dialogs.ShowLaunchSettings(pack.id); break;
	case ManageAction::Repair: m_plays.Repair(pack); break;
	case ManageAction::Update: m_dialogs.CheckForUpdate(pack); break;
	case ManageAction::Server: m_navigator.ShowServer(pack.id); break;
	case ManageAction::Uninstall: m_dialogs.ConfirmUninstall(pack.id, request.versionKey); break;
	case ManageAction::SelectVersion:
		SelectInstalledVersion(pack, request.versionKey);
		m_library.Save();
		break;
	case ManageAction::UndoChanges:
		if (m_content.UndoChanges(pack))
			m_page.Refresh();
		break;
	default: break;
	}
}

void ManagePageHost::Draw(const PageFrame& frame)
{
	Modpack& pack = *m_library.Find(m_packId);
	const ManageRequest request = m_page.Draw(BuildContext(pack, frame));
	Handle(pack, request);
}

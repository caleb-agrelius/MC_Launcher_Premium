#pragma once

#include "Core/Runtime.h"
#include "Core/UiQueue.h"
#include "Core/WorkerPool.h"
#include "Network/HttpClient.h"
#include "Services/Account/AccountService.h"
#include "Services/Discover/DiscoverService.h"
#include "Services/Discover/OptionalModsService.h"
#include "Services/Discover/PackDetailsService.h"
#include "Services/Downloads/DownloadService.h"
#include "Services/Library/CustomPackService.h"
#include "Services/Library/PackContentService.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Library/UninstallService.h"
#include "Services/Play/PlayService.h"
#include "Services/Servers/ServerService.h"
#include "Services/Settings/JavaScanService.h"
#include "Services/Settings/SettingsStore.h"
#include "Services/Settings/SharedFilesService.h"
#include "Services/Updates/UpdateService.h"
#include "UI/Common/VersionPicker.h"
#include "UI/Dialogs/DeleteServerDialog.h"
#include "UI/Dialogs/DialogHost.h"
#include "UI/Dialogs/LaunchSettingsDialog.h"
#include "UI/Dialogs/NewPackDialogHost.h"
#include "UI/Dialogs/OptionalModsDialog.h"
#include "UI/Dialogs/StopGameDialog.h"
#include "UI/Dialogs/UninstallDialog.h"
#include "UI/Dialogs/UpdateDialog.h"
#include "UI/ImageCache.h"
#include "UI/Pages/Discover/DiscoverDetails.h"
#include "UI/Pages/Discover/DiscoverFilterBar.h"
#include "UI/Pages/Discover/DiscoverPage.h"
#include "UI/Pages/Discover/DiscoverResultList.h"
#include "UI/Pages/Discover/PackInfoTabs.h"
#include "UI/Pages/Discover/PackInstallSection.h"
#include "UI/Pages/Discover/PackPlayButton.h"
#include "UI/Pages/Downloads/DownloadsPage.h"
#include "UI/Pages/Library/LibraryPackCard.h"
#include "UI/Pages/Library/LibraryPage.h"
#include "UI/Pages/Manage/ManagePageHost.h"
#include "UI/Pages/Servers/ServerConsole.h"
#include "UI/Pages/Servers/ServerDetails.h"
#include "UI/Pages/Servers/ServerDockerTab.h"
#include "UI/Pages/Servers/ServerFilesCard.h"
#include "UI/Pages/Servers/ServerList.h"
#include "UI/Pages/Servers/ServerModsTab.h"
#include "UI/Pages/Servers/ServerRunCard.h"
#include "UI/Pages/Servers/ServerSettingsTab.h"
#include "UI/Pages/Servers/ServersPage.h"
#include "UI/Pages/Settings/SettingsPageHost.h"
#include "UI/Shell/AccountPanel.h"
#include "UI/Shell/AppShell.h"
#include "UI/Shell/Mascot.h"
#include "UI/Shell/NavigatorProxy.h"
#include "UI/Shell/Shortcuts.h"
#include "UI/Shell/Sidebar.h"
#include "UI/Shell/ToastNotifier.h"

#include <atomic>
#include <filesystem>
#include <ostream>
#include <vector>

class GlfwWindow;
class ThemeController;

// The composition root: builds every service and screen, hands each the abstractions it needs,
// and runs a frame. This is the one place that knows which concrete class stands behind each
// interface. Members are declared in dependency order, so they are destroyed in the reverse:
// screens, then services (whose destructors wait for their background tasks), then the worker
// pool and network they were using.
class AppRoot {
public:
	AppRoot(const std::filesystem::path& dataRoot, std::ostream& log, GlfwWindow& window, SettingsStore& settings,
		ThemeController& theme);

	// One frame, from running what workers posted to presenting the picture.
	void Frame(const std::vector<std::filesystem::path>& droppedPaths);

	// Stops everything in flight and saves: running servers get "stop", workers are asked to
	// finish and waited for, settings are written with the window's last position.
	void Shutdown();

private:
	std::ostream& m_log;
	GlfwWindow& m_window;
	SettingsStore& m_settings;
	ThemeController& m_theme;

	// Infrastructure.
	std::atomic<bool> m_shuttingDown{ false };
	WorkerPool m_workers;
	UiQueue m_uiQueue;
	HttpClient m_http;
	ToastNotifier m_notifier;
	Runtime m_rt;

	// Services.
	PackLibrary m_library;
	ImageCache m_images;
	AccountService m_accounts;
	DownloadService m_downloads;
	PlayService m_plays;
	UninstallService m_uninstaller;
	UpdateService m_updates;
	ServerService m_servers;
	JavaScanService m_javas;
	SharedFilesService m_sharedFiles;
	CustomPackService m_customPacks;
	PackContentService m_content;
	DiscoverService m_discover;
	PackDetailsService m_details;
	OptionalModsService m_optional;

	// Screens.
	NavigatorProxy m_navigator;
	Mascot m_mascot;
	VersionPicker m_versions;
	SettingsPageHost m_settingsPage;
	StopGameDialog m_stopDialog;
	LaunchSettingsDialog m_launchDialog;
	NewPackDialogHost m_newPackDialog;
	OptionalModsDialog m_optionalDialog;
	DeleteServerDialog m_deleteServerDialog;
	UninstallDialog m_uninstallDialog;
	UpdateDialog m_updateDialog;
	DialogHost m_dialogs;

	DiscoverFilterBar m_filterBar;
	DiscoverResultList m_resultList;
	PackInstallSection m_installSection;
	PackInfoTabs m_infoTabs;
	PackPlayButton m_playButton;
	DiscoverDetails m_discoverDetails;
	DiscoverPage m_discoverPage;

	LibraryPackCard m_libraryCard;
	LibraryPage m_libraryPage;
	DownloadsPage m_downloadsPage;
	ManagePageHost m_managePage;

	ServerFilesCard m_serverFiles;
	ServerRunCard m_serverRun;
	ServerConsole m_serverConsole;
	ServerSettingsTab m_serverSettings;
	ServerModsTab m_serverMods;
	ServerDockerTab m_serverDocker;
	ServerList m_serverList;
	ServerDetails m_serverDetails;
	ServersPage m_serversPage;

	AccountPanel m_accountPanel;
	Sidebar m_sidebar;
	Shortcuts m_shortcuts;
	AppShell m_shell;
};

#include "App/AppRoot.h"

#include "App/GlfwWindow.h"
#include "App/ThemeController.h"
#include "Model/Modpack.h"
#include "Network/CurseForge.h"
#include "Services/AppPaths.h"
#include "UI/Theme.h"
#include "UI/Toasts.h"
#include "imgui.h"
#include "Util/TracedFs.h"

#include <chrono>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

AppRoot::AppRoot(const fs::path& dataRoot, std::ostream& log, GlfwWindow& window, SettingsStore& settings,
	ThemeController& theme)
	: m_log(log), m_window(window), m_settings(settings), m_theme(theme),
	  m_workers([] { HttpClient::ReleaseThreadHandle(); }),
	  m_tracedHttp(m_http),
	  m_rt{ m_workers, m_uiQueue, m_tracedHttp, m_notifier, log, m_shuttingDown },
	  m_library(AppPaths::kLibraryFile, log),
	  m_images(m_tracedHttp),
	  m_accounts(m_rt, settings, AppPaths::kAccountFile),
	  m_downloads(m_rt, m_library, AppPaths::kDownloadsFile),
	  m_plays(m_rt, settings, m_library, m_accounts, m_downloads, window),
	  m_uninstaller(m_rt, m_library, m_downloads, m_plays),
	  m_updates(m_rt, m_library, m_downloads, m_plays, m_uninstaller),
	  m_servers(m_rt, settings, m_library),
	  m_javas(m_rt),
	  m_sharedFiles(m_rt, settings, m_library),
	  m_customPacks(m_rt, m_library, m_accounts),
	  m_content(m_rt, m_library, m_plays),
	  m_discover(m_rt, settings, LoadIndex(AppPaths::kIndexFile), [] { return ImGui::GetTime(); }),
	  m_details(m_rt),
	  m_optional(m_rt, m_library, m_downloads),
	  m_mascot(dataRoot.empty() ? fs::path() : dataRoot / "src" / "assets" / "minecraft_fox.glb", log),
	  m_versions(m_details),
	  m_settingsPage(settings, m_accounts, m_javas, m_sharedFiles, m_plays, m_discover, theme),
	  m_stopDialog(m_plays),
	  m_launchDialog(m_library, settings, m_settingsPage),
	  m_newPackDialog(m_rt, m_customPacks, m_navigator),
	  m_optionalDialog(m_optional),
	  m_deleteServerDialog(m_servers, m_library),
	  m_uninstallDialog(m_library, m_uninstaller, m_servers, m_plays, m_downloads, m_updates, settings),
	  m_updateDialog(m_library, m_updates, m_plays, m_downloads),
	  m_dialogs(m_stopDialog, m_launchDialog, m_newPackDialog, m_optionalDialog, m_deleteServerDialog,
		m_uninstallDialog, m_updateDialog),
	  m_filterBar(m_discover, settings),
	  m_resultList(m_discover, settings, m_images),
	  m_installSection(m_details, m_downloads, m_library, m_versions, m_optional),
	  m_infoTabs(m_details),
	  m_playButton(m_library, m_plays, m_navigator),
	  m_discoverDetails(m_details, settings, m_images, m_installSection, m_playButton, m_infoTabs),
	  m_discoverPage(m_discover, m_filterBar, m_resultList, m_discoverDetails),
	  m_libraryCard(m_library, m_plays, m_updates, m_servers, m_images, m_navigator, m_dialogs),
	  m_libraryPage(m_library, m_libraryCard, m_navigator, m_dialogs),
	  m_downloadsPage(m_downloads, m_images, m_navigator),
	  m_managePage(m_rt, m_library, m_plays, m_updates, m_servers, m_content, settings, m_images, m_navigator, m_dialogs),
	  m_serverFiles(m_servers, m_library, m_versions, m_dialogs),
	  m_serverRun(m_servers),
	  m_serverSettings(m_servers),
	  m_serverMods(m_servers),
	  m_serverDocker(m_servers),
	  m_serverList(m_servers, m_library, settings, m_images),
	  m_serverDetails(m_servers, m_library, m_images, m_dialogs, m_serverFiles, m_serverRun, m_serverConsole,
		m_serverSettings, m_serverMods, m_serverDocker),
	  m_serversPage(m_servers, m_library, m_serverList, m_serverDetails, m_navigator),
	  m_accountPanel(m_accounts, settings),
	  m_sidebar(m_mascot, m_accountPanel, m_downloads, m_servers, m_navigator),
	  m_shortcuts(settings, m_navigator, m_discoverPage),
	  m_shell(m_sidebar, m_dialogs, m_shortcuts,
		AppShell::Pages{ m_discoverPage, m_libraryPage, m_downloadsPage, m_serversPage, m_settingsPage, m_managePage },
		m_notifier)
{
	m_navigator.Bind(m_shell);
	std::error_code ec;
	TracedFs::CreateDirectories(AppPaths::kDownloadsRoot, ec);
	TracedFs::CreateDirectories(AppPaths::kInstallsRoot, ec);
	log << "Loaded " << m_library.Packs().size() << " library pack(s)\n";
	SetCurseForgeApiKey(settings.Get().curseForgeApiKey);
	m_downloads.LoadSaved();
	m_sharedFiles.Start();
	m_mascot.Roll();
	m_mascot.SetReduceMotion(settings.Get().reduceMotion);
	log.flush();
}

void AppRoot::Frame(const std::vector<fs::path>& droppedPaths)
{
	m_workers.Reap();
	m_uiQueue.Drain();
	m_theme.Update();
	m_window.TrackPlacement();
	m_mascot.SetReduceMotion(m_settings.Get().reduceMotion);
	// Save after edits, but not on every keystroke or slider step.
	m_settings.SaveIfSettled(ImGui::IsAnyItemActive());
	m_images.Update(ImGui::GetTime());

	m_window.BeginFrame();
	m_shell.Draw(droppedPaths);
	Toasts::Draw(m_settings.Get().reduceMotion);
	m_window.EndFrame(Theme::C().windowBg);
}

void AppRoot::Shutdown()
{
	m_servers.StopAll(); // running servers get "stop" (they save their worlds), then a few seconds before being killed
	// Ask in-flight downloads and extractions to stop, then wait for them.
	m_shuttingDown.store(true);
	m_accounts.CancelSignIn();
	m_plays.CancelAll(); // a running Minecraft keeps running
	m_workers.JoinAll();
	m_uiQueue.Drain(); // saves downloads and extractions that finished during shutdown

	// Remember the window for next time (and any unsaved settings).
	m_settings.Get().window = m_window.Placement();
	m_settings.SaveNow();
	m_images.Shutdown(); // frees textures while the GL context still exists
	m_mascot.Shutdown();
}

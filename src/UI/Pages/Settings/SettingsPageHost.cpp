#include "UI/Pages/Settings/SettingsPageHost.h"

#include "Core/AppInfo.h"
#include "Network/CurseForge.h"
#include "Platform/Platform.h"
#include "Services/Account/AccountService.h"
#include "Services/Discover/DiscoverService.h"
#include "Services/Play/PlayService.h"
#include "Services/Settings/JavaScanService.h"
#include "Services/Settings/SettingsStore.h"
#include "Services/Settings/SharedFilesService.h"
#include "UI/Common/UiHelpers.h"
#include "Util/SharedFiles.h"

#include <filesystem>

namespace fs = std::filesystem;

SettingsPageHost::SettingsPageHost(SettingsStore& settings, AccountService& accounts, JavaScanService& javas,
	SharedFilesService& sharedFiles, PlayService& plays, DiscoverService& discover, IDisplayMetrics& display)
	: m_settings(settings), m_accounts(accounts), m_javas(javas), m_sharedFiles(sharedFiles), m_plays(plays),
	  m_discover(discover), m_display(display)
{
}

void SettingsPageHost::DrawHeader()
{
	UiHelpers::DrawPageHeader("LAUNCHER  /  SETTINGS", "Settings", "Themes, Minecraft launch defaults, and more.");
}

SettingsPageContext SettingsPageHost::Context(double shownAt)
{
	SettingsPageContext context;
	context.appVersion = AppInfo::kVersion;
	std::error_code ec;
	context.dataFolder = fs::current_path(ec).string();
	context.systemMemoryMb = Platform::SystemMemoryMb();
	context.dpiScale = m_display.DpiScale();
	context.curseForgeEnvKeySet = !CurseForgeEnvApiKey().empty();
	context.shownAt = shownAt;

	const Account& account = m_accounts.Active();
	context.accountName = account.type == Account::Type::None ? std::string() : account.name;
	context.accountCount = static_cast<int>(m_accounts.List().accounts.size());
	context.accountKind = account.type == Account::Type::Microsoft ? "Microsoft account" : "No account yet";

	context.javas = &m_javas.Installs();
	context.javasScanned = m_javas.Scanned();
	context.javasScanning = m_javas.Scanning();
	context.scanJavas = [this]() { m_javas.Scan(); };

	context.sharingStats = m_sharedFiles.Stats();
	context.sharingBusy = m_sharedFiles.Busy();
	context.sharingStatus = m_sharedFiles.Status();
	// Sharing existing files renames mod jars, which a running game has open.
	context.sharingBlocked = m_plays.AnyBusy();
	context.shareExisting = [this]() { m_sharedFiles.ShareExisting(); };
	context.countSharing = [this]() { m_sharedFiles.Count(); };
	return context;
}

bool SettingsPageHost::DrawPackLaunch(Settings& settings, const std::string& packId)
{
	return m_page.DrawPackLaunch(settings, packId, Context(0.0));
}

void SettingsPageHost::Draw(const PageFrame& frame)
{
	Settings& settings = m_settings.Get();
	const std::string oldCurseForgeKey = CurseForgeApiKey();
	if (!m_page.Draw(settings, Context(frame.shownAt)))
		return;
	m_settings.MarkDirty();
	SetCurseForgeApiKey(settings.curseForgeApiKey);
	if (SharedFiles::Enabled() != settings.shareModFiles)
		m_sharedFiles.ApplySetting();
	// A new key: refresh Discover's search on the next visit.
	if (CurseForgeApiKey() != oldCurseForgeKey) {
		auto& views = m_discover.Views();
		const auto curseForge = views.find(PackSource::CurseForge);
		if (curseForge != views.end())
			curseForge->second.searchPending = true;
	}
}

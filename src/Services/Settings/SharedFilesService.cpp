#include "Services/Settings/SharedFilesService.h"

#include "Services/AppPaths.h"
#include "Util/SharedFiles.h"
#include "Util/Text.h"

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string DescribeSharing(const SharedFiles::Stats& stats)
{
	if (stats.files == 0)
		return "Nothing is shared yet.";
	return std::to_string(stats.files) + " file(s) stored (" + FormatFileSize(stats.bytes) + "), " +
		std::to_string(stats.shared) + " used by more than one pack, " + FormatFileSize(stats.saved) + " saved.";
}

fs::path StoreDir()
{
	return fs::absolute(fs::path(AppPaths::kGameRoot) / "shared-files");
}

}

SharedFilesService::SharedFilesService(const Runtime& runtime, SettingsStore& settings, PackLibrary& library)
	: m_rt(runtime), m_settings(settings), m_library(library), m_scope(runtime.tasks)
{
}

void SharedFilesService::Start()
{
	SharedFiles::Configure(StoreDir(), m_settings.Get().shareModFiles);
	// Stored files no pack uses any more (after uninstalls outside this run, too).
	m_scope.Run([this]() {
		const std::uint64_t freed = SharedFiles::Prune();
		if (freed > 0)
			m_rt.ui.Post([this, freed]() { m_rt.log << "Shared files: removed " << freed << " unused byte(s)\n"; });
	});
}

void SharedFilesService::ApplySetting()
{
	if (SharedFiles::Enabled() != m_settings.Get().shareModFiles)
		SharedFiles::Configure(StoreDir(), m_settings.Get().shareModFiles);
}

void SharedFilesService::Count()
{
	if (m_busy)
		return;
	m_busy = true;
	m_scope.Run([this]() {
		const std::string text = DescribeSharing(SharedFiles::StoreStats());
		m_rt.ui.Post([this, text]() {
			m_busy = false;
			m_stats = text;
		});
	});
}

void SharedFilesService::ShareExisting()
{
	if (m_busy)
		return;
	// Every installed version's game folder, and every server folder.
	std::vector<fs::path> gameDirs;
	for (const auto& [id, pack] : m_library.Packs()) {
		for (const InstalledVersion& v : pack.installedVersions)
			gameDirs.push_back(fs::absolute(fs::u8path(v.gameDir)));
	}
	std::error_code ec;
	if (fs::is_directory(AppPaths::kServersRoot, ec)) {
		for (const auto& entry : fs::directory_iterator(AppPaths::kServersRoot, ec))
			if (entry.is_directory(ec))
				gameDirs.push_back(fs::absolute(entry.path()));
	}
	m_busy = true;
	m_status = "Looking for identical files...";
	m_rt.log << "Sharing files in " << gameDirs.size() << " folder(s)\n";
	m_scope.Run([this, gameDirs]() {
		auto progress = [&](std::size_t done, std::size_t total) {
			if (done % 25 == 0 || done == total)
				m_rt.ui.Post([this, done, total]() {
					m_status = "Checking files (" + std::to_string(done) + "/" + std::to_string(total) + ")";
				});
		};
		const SharedFiles::ShareResult result = SharedFiles::ShareExisting(gameDirs, progress, &m_rt.shuttingDown);
		const std::string stats = DescribeSharing(SharedFiles::StoreStats());
		m_rt.ui.Post([this, result, stats]() {
			m_busy = false;
			m_stats = stats;
			m_status = "Checked " + std::to_string(result.scanned) + " file(s): " +
				(result.linked == 0 ? std::string("no new duplicates.")
					: std::to_string(result.linked) + " now shared, " + FormatFileSize(result.saved) + " freed.") +
				(result.error.empty() ? std::string() : " " + result.error + ".");
			m_rt.log << "Shared " << result.linked << " of " << result.scanned << " file(s), freed " << result.saved << " bytes"
				<< (result.error.empty() ? std::string() : ": " + result.error) << "\n";
			m_rt.notify.Notify(NoticeKind::Success, "Shared mod files", m_status);
		});
	});
}

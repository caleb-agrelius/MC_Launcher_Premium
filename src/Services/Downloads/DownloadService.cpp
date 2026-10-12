#include "Services/Downloads/DownloadService.h"

#include "Game/InstanceContent.h"

#include "Network/PackSources.h"
#include "Services/AppPaths.h"
#include "Storage/Installer.h"
#include "Storage/Library.h"
#include "Util/TracedFs.h"

#include <filesystem>

namespace fs = std::filesystem;

DownloadService::DownloadService(const Runtime& runtime, PackLibrary& library, std::string downloadsFile)
	: m_rt(runtime), m_library(library), m_file(std::move(downloadsFile)), m_scope(runtime.tasks)
{
}

void DownloadService::LoadSaved()
{
	Library& packs = m_library.Packs();
	for (SavedDownload& saved : LoadSavedDownloads(m_file)) {
		std::error_code existsError;
		if (!fs::is_regular_file(saved.outPath, existsError))
			continue;
		DownloadState state;
		state.fileName = std::move(saved.fileName);
		state.outPath = std::move(saved.outPath);
		{
			const auto entry = packs.find(saved.pack.id);
			state.installed = entry != packs.end() && FindInstalledVersion(entry->second, VersionKey(saved.pack));
		}
		state.pack = std::move(saved.pack);
		m_downloads[saved.url] = std::move(state);
	}
	m_rt.log << "Loaded " << m_downloads.size() << " saved download(s)\n";
}

void DownloadService::Save()
{
	std::vector<SavedDownload> saved;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& [url, state] : m_downloads) {
			if (!state.outPath.empty())
				saved.push_back({ url, state.fileName, state.outPath, state.pack });
		}
	}
	if (!SaveDownloads(m_file, saved))
		m_rt.log << "Could not save " << m_file << "\n";
}

bool DownloadService::Find(const std::string& key, DownloadState& out) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_downloads.find(key);
	if (it == m_downloads.end())
		return false;
	out = it->second;
	return true;
}

std::vector<std::pair<std::string, DownloadState>> DownloadService::Snapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return std::vector<std::pair<std::string, DownloadState>>(m_downloads.begin(), m_downloads.end());
}

int DownloadService::ActiveCount() const
{
	int count = 0;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& [url, state] : m_downloads) {
		if (state.active || state.installing)
			++count;
	}
	return count;
}

bool DownloadService::IsBusyFor(const std::string& packId) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& [key, state] : m_downloads) {
		if (state.pack.id == packId && (state.active || state.installing))
			return true;
	}
	return false;
}

void DownloadService::Forget(const std::string& packId, bool wholePack, const std::string& versionKey)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto it = m_downloads.begin(); it != m_downloads.end();) {
			const bool match = it->second.pack.id == packId && !it->second.active &&
				(wholePack || VersionKey(it->second.pack) == versionKey);
			if (match)
				it = m_downloads.erase(it);
			else
				++it;
		}
	}
	Save();
}

void DownloadService::StartInstall(const std::string& url, const Modpack& pack, const std::string& zipPath)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		DownloadState& current = m_downloads[url];
		current.installing = true;
		current.installProgress = 0.0;
		current.installError.clear();
	}
	// Each version gets its own folder, so versions can be installed side by side.
	const InstalledVersion version = PlanInstalledVersion(m_library.Packs(), pack, false);
	const std::string targetDir = version.installDir;
	m_scope.Run([this, zipPath, targetDir, url, pack, version]() {
		std::string error;
		// Start from an empty folder, so another version's overrides don't linger.
		std::error_code clearError;
		TracedFs::RemoveAll(targetDir, clearError);
		auto progress = [&](int total, int now) {
			std::lock_guard<std::mutex> lock(m_mutex);
			DownloadState& current = m_downloads[url];
			if (total > 0)
				current.installProgress = static_cast<double>(now) / static_cast<double>(total);
		};
		const bool ok = InstallZip(zipPath, targetDir, progress, &error, &m_rt.shuttingDown);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			DownloadState& current = m_downloads[url];
			current.installing = false;
			if (ok)
				current.installed = true;
			else
				current.installError = error;
		}
		if (ok) {
			m_rt.ui.Post([this, pack, version]() {
				AddInstalledVersion(m_library.Packs(), pack, version);
				m_library.Save();
				m_rt.notify.Notify(NoticeKind::Success, "Files extracted", pack.name + " " + pack.version + " is in your library.");
				if (m_observer)
					m_observer(pack, true);
			});
		} else {
			m_rt.ui.Post([this, pack, error, cancelled = m_rt.shuttingDown.load()]() {
				if (!cancelled)
					m_rt.notify.Notify(NoticeKind::Error, "Extraction failed", pack.name + ": " + error);
				if (m_observer)
					m_observer(pack, false);
			});
		}
	});
}

void DownloadService::StartDownloadInstall(const Modpack& pack, const PackFile& file)
{
	DownloadState started;
	started.fileName = file.name;
	started.pack = pack;
	started.active = true;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_downloads[file.url] = started;
	}
	const std::string url = file.url;
	// Per-pack folder so same-named files from different packs don't collide.
	const fs::path packDownloadDir = fs::path(AppPaths::kDownloadsRoot) / pack.id;
	const std::string outputPath = (packDownloadDir / file.name).string();
	m_scope.Run([this, url, packDownloadDir, outputPath, pack]() {
		std::string error;
		std::error_code dirError;
		TracedFs::CreateDirectories(packDownloadDir, dirError);
		auto progress = [&](double total, double now) {
			std::lock_guard<std::mutex> lock(m_mutex);
			DownloadState& current = m_downloads[url];
			current.total = total;
			current.now = now;
		};
		const bool ok = m_rt.http.DownloadToFile(url, outputPath, progress, &error, &m_rt.shuttingDown);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			DownloadState& current = m_downloads[url];
			current.active = false;
			if (ok) {
				current.outPath = outputPath;
				current.installing = !m_rt.shuttingDown.load(); // extraction starts next frame
			} else {
				current.error = error;
			}
		}
		if (ok)
			m_rt.ui.Post([this]() { Save(); });
		if (m_rt.shuttingDown.load())
			return; // the file stays downloaded; Install files on Downloads extracts it later
		if (ok)
			m_rt.ui.Post([this, url, pack, outputPath]() { StartInstall(url, pack, outputPath); });
		else
			m_rt.ui.Post([this, pack, error]() {
				m_rt.notify.Notify(NoticeKind::Error, "Download failed", pack.name + ": " + error);
				if (m_observer)
					m_observer(pack, false);
			});
	});
}

void DownloadService::StartPlanInstall(const Modpack& pack)
{
	const std::string key = "pack:" + pack.id + "@" + VersionKey(pack);
	const InstalledVersion version = PlanInstalledVersion(m_library.Packs(), pack, true);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		DownloadState started;
		started.fileName = pack.version.empty() ? pack.name : pack.name + " " + pack.version;
		started.pack = pack;
		started.active = true;
		started.planInstall = true;
		started.stage = "Preparing";
		m_downloads[key] = std::move(started);
	}
	m_rt.log << "Installing " << pack.id << " " << pack.version << " from " << PackSourceName(pack.source) << "\n";
	m_scope.Run([this, key, pack, version]() {
		auto setStage = [&](const std::string& stage, std::uint64_t done, std::uint64_t total) {
			std::lock_guard<std::mutex> lock(m_mutex);
			DownloadState& current = m_downloads[key];
			current.stage = stage;
			current.stageDone = done;
			current.stageTotal = total;
		};
		std::string error;
		InstallPlan plan;
		std::vector<PlannedFile> blocked;
		const std::string workDir = (fs::path(AppPaths::kDownloadsRoot) / pack.id).string();
		std::error_code dirError;
		TracedFs::CreateDirectories(workDir, dirError);
		bool ok = BuildInstallPlan(m_rt.http, pack, workDir, plan,
			[&](const std::string& status) { setStage(status, 0, 0); }, &error, &m_rt.shuttingDown);
		if (ok) {
			plan.skipFiles = RemovedContent(version.contentChanges);
			ok = RunInstallPlan(m_rt.http, plan, version.installDir,
				[&](const InstallProgress& progress) { setStage(progress.stage, progress.done, progress.total); },
				blocked, &error, &m_rt.shuttingDown);
		}
		// The pack's override zips can bring back files the player removed or switched.
		if (ok)
			ApplyContentChanges(fs::u8path(version.gameDir), version.contentChanges);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			DownloadState& current = m_downloads[key];
			current.active = false;
			current.installed = ok;
			current.error = ok ? std::string() : error;
			current.blocked = blocked;
			current.notes = plan.notes;
		}
		const bool cancelled = m_rt.shuttingDown.load();
		m_rt.ui.Post([this, pack, version, ok, cancelled, error,
			blockedCount = blocked.size(), fileCount = plan.files.size()]() {
			if (!ok) {
				m_rt.log << "Install of " << pack.id << " failed: " << error << "\n";
				if (!cancelled)
					m_rt.notify.Notify(NoticeKind::Error, "Install failed", pack.name + ": " + error);
				if (m_observer)
					m_observer(pack, false);
				return;
			}
			m_rt.log << "Installed " << pack.id << ": " << fileCount << " file(s), " << blockedCount << " blocked\n";
			AddInstalledVersion(m_library.Packs(), pack, version);
			m_library.Save();
			m_rt.notify.Notify(NoticeKind::Success, "Pack installed", blockedCount > 0
				? pack.name + " " + pack.version + ": " + std::to_string(blockedCount) + " file(s) need a manual download."
				: pack.name + " " + pack.version + " is in your library.");
			if (m_observer)
				m_observer(pack, true);
		});
	});
}

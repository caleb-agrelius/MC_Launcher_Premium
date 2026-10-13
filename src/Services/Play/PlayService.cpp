#include "Services/Play/PlayService.h"

#include "Game/Launcher.h"
#include "Network/MicrosoftAuth.h"
#include "Network/PackSources.h"
#include "Services/AppPaths.h"

#include <chrono>
#include <filesystem>
#include <iterator>

namespace fs = std::filesystem;

PlayService::PlayService(const Runtime& runtime, SettingsStore& settings, PackLibrary& library, AccountService& accounts,
	DownloadService& downloads, IWindowControl& window)
	: m_rt(runtime), m_settings(settings), m_library(library), m_accounts(accounts), m_downloads(downloads),
	  m_window(window), m_scope(runtime.tasks)
{
}

std::string PlayService::Key(const Modpack& pack) const
{
	const InstalledVersion* v = m_library.ActiveVersionOf(pack);
	return pack.id + "@" + (v ? v->key : VersionKey(pack));
}

PlayState PlayService::StateOf(const std::string& key) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_plays.find(key);
	return it != m_plays.end() ? it->second : PlayState();
}

bool PlayService::VersionBusy(const std::string& packId, const std::string& versionKey) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const std::string prefix = packId + "@";
	for (const auto& [key, play] : m_plays) {
		const bool match = versionKey.empty() ? key.rfind(prefix, 0) == 0 : key == prefix + versionKey;
		if (match && (play.busy || play.playing))
			return true;
	}
	return false;
}

bool PlayService::AnyBusy() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& [key, play] : m_plays) {
		if (play.busy || play.playing)
			return true;
	}
	return false;
}

void PlayService::RecordError(const std::string& key, const std::string& message)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_plays[key].error = message;
}

void PlayService::Cancel(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_plays.find(key);
	if (it == m_plays.end() || !it->second.busy || it->second.playing)
		return;
	it->second.cancel->store(true);
	it->second.step = kCancellingStep;
	it->second.progress = -1.0;
}

void PlayService::Stop(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_plays.find(key);
	if (it != m_plays.end())
		it->second.stop->store(true);
}

void PlayService::CancelAll()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto& [packId, play] : m_plays)
		play.cancel->store(true);
}

void PlayService::Forget(const std::string& packId, bool wholePack, const std::string& versionKey)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const std::string prefix = packId + "@";
	for (auto it = m_plays.begin(); it != m_plays.end();) {
		const bool removed = wholePack ? it->first.rfind(prefix, 0) == 0 : it->first == prefix + versionKey;
		it = removed ? m_plays.erase(it) : std::next(it);
	}
}

void PlayService::Start(const Modpack& pack)
{
	const Account account = m_accounts.Active();
	const Settings& settings = m_settings.Get();
	if (!CanLaunchAs(account)) {
		RecordError(Key(pack), "Sign in with a Microsoft account that owns Minecraft: Java Edition first.");
		return;
	}
	if (settings.microsoftClientId.empty()) {
		RecordError(Key(pack), "Playing needs Microsoft sign-in, which needs a client ID approved by Mojang. "
			"Set it in Settings > Account.");
		return;
	}
	auto cancel = std::make_shared<std::atomic<bool>>(false);
	auto stop = std::make_shared<std::atomic<bool>>(false);
	const std::string stateKey = Key(pack);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		PlayState& state = m_plays[stateKey];
		state = PlayState{};
		state.busy = true;
		state.step = "Starting";
		state.cancel = cancel;
		state.stop = stop;
	}
	PlayRequest request;
	request.gameRoot = fs::absolute(AppPaths::kGameRoot);
	request.extractedDir = fs::absolute(m_library.InstallDir(pack));
	request.instanceDir = fs::absolute(m_library.GameDir(pack));
	request.contentChanges = m_library.ContentChangesOf(pack);
	request.account = account;
	request.launch = LaunchSettingsFor(settings, pack.id);
	request.gameOptions = settings.gameOptions;
	request.stopGame = stop.get(); // kept alive by the worker's copy of stop
	const std::string clientId = settings.microsoftClientId;
	const std::string packId = pack.id;
	const std::string packName = pack.name;
	const AfterLaunch afterLaunch = settings.launch.afterLaunch;
	m_rt.log << "Play " << packId << " as " << account.name << "\n";
	m_rt.log.flush();
	m_scope.Run([this, request, clientId, packId, stateKey, packName, afterLaunch, cancel, stop]() mutable {
		bool started = false; // Minecraft's process is running
		std::chrono::steady_clock::time_point startedAt;
		auto progress = [&](const std::string& step, double fraction) {
			std::lock_guard<std::mutex> lock(m_mutex);
			PlayState& state = m_plays[stateKey];
			state.step = cancel->load() ? std::string(kCancellingStep) : step;
			state.progress = fraction;
			state.playing = step == "Playing";
			if (state.playing && !started) {
				started = true;
				startedAt = std::chrono::steady_clock::now();
				// "When the game starts" from Settings > Minecraft.
				m_rt.ui.Post([this, afterLaunch, packName]() {
					if (afterLaunch == AfterLaunch::Minimize)
						m_window.Minimize();
					else if (afterLaunch == AfterLaunch::Close)
						m_window.RequestClose(); // the game keeps running
					else
						m_rt.notify.Notify(NoticeKind::Success, "Minecraft started", packName);
				});
			}
		};
		std::string error;
		// Every launch checks the account with Microsoft and Minecraft services first (sign-in,
		// ownership and profile), so a saved account that lost access, or one that never had it,
		// can't play. There's no way to play without this check passing.
		progress("Checking Microsoft account", -1.0);
		bool ok = RefreshMicrosoftAccount(m_rt.http, clientId, request.account, &error, cancel.get());
		if (ok) {
			const Account refreshed = request.account;
			m_rt.ui.Post([this, refreshed]() { m_accounts.Update(refreshed); });
		}
		try {
			if (ok)
				ok = PlayModpack(m_rt.http, request, progress, &error, cancel.get());
		} catch (const std::exception& ex) {
			ok = false;
			error = std::string("Unexpected error: ") + ex.what();
		}
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			PlayState& state = m_plays[stateKey];
			state.busy = false;
			state.playing = false;
			state.error = (ok || cancel->load() || stop->load()) ? std::string() : error;
		}
		const bool cancelled = cancel->load() || stop->load();
		const bool stopped = stop->load();
		const std::int64_t playedSeconds = started
			? std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startedAt).count() : 0;
		m_rt.ui.Post([this, packId, packName, ok, cancelled, stopped, started, afterLaunch, error, playedSeconds]() {
			if (playedSeconds > 0) {
				if (Modpack* played = m_library.Find(packId)) {
					played->playSeconds += playedSeconds;
					m_library.Save();
				}
			}
			if (ok)
				m_rt.log << "Minecraft closed (" << packId << ")\n";
			else
				m_rt.log << "Play " << packId << " failed: " << error << "\n";
			if (started && afterLaunch == AfterLaunch::Minimize)
				m_window.Restore();
			if (stopped)
				m_rt.notify.Notify(NoticeKind::Info, "Minecraft was closed", packName);
			else if (!ok && !cancelled)
				m_rt.notify.Notify(NoticeKind::Error, "Couldn't play " + packName, error);
		});
	});
}

void PlayService::Repair(const Modpack& pack)
{
	if (GetSourceCapabilities(pack.source).installsPacks) {
		m_downloads.StartPlanInstall(pack);
		m_rt.notify.Notify(NoticeKind::Info, "Repairing " + pack.name, "Progress is on the Downloads page.");
		return;
	}
	auto cancel = std::make_shared<std::atomic<bool>>(false);
	const std::string stateKey = Key(pack);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		PlayState& state = m_plays[stateKey];
		state = PlayState{};
		state.busy = true;
		state.step = "Repairing";
		state.cancel = cancel;
	}
	PlayRequest request;
	request.gameRoot = fs::absolute(AppPaths::kGameRoot);
	request.extractedDir = fs::absolute(m_library.InstallDir(pack));
	request.instanceDir = fs::absolute(m_library.GameDir(pack));
	request.contentChanges = m_library.ContentChangesOf(pack);
	const std::string packId = pack.id;
	const std::string packName = pack.name;
	m_rt.log << "Repair " << packId << " " << pack.version << "\n";
	m_scope.Run([this, request, packId, packName, cancel, stateKey]() {
		auto progress = [&](const std::string& step, double fraction) {
			std::lock_guard<std::mutex> lock(m_mutex);
			PlayState& state = m_plays[stateKey];
			state.step = cancel->load() ? std::string(kCancellingStep) : step;
			state.progress = fraction;
		};
		std::string error;
		bool ok = false;
		try {
			ok = RepairModpack(m_rt.http, request, progress, &error, cancel.get());
		} catch (const std::exception& ex) {
			error = std::string("Unexpected error: ") + ex.what();
		}
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			PlayState& state = m_plays[stateKey];
			state.busy = false;
			state.error = (ok || cancel->load()) ? std::string() : error;
		}
		const bool cancelled = cancel->load();
		m_rt.ui.Post([this, packId, packName, ok, cancelled, error]() {
			m_rt.log << "Repair " << packId << (ok ? " done" : " failed: " + error) << "\n";
			if (ok)
				m_rt.notify.Notify(NoticeKind::Success, "Repaired " + packName, "Every pack file matches its checksum.");
			else if (!cancelled)
				m_rt.notify.Notify(NoticeKind::Error, "Repair failed", packName + ": " + error);
		});
	});
}

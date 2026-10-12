#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Settings/SettingsStore.h"

#include <string>

// Shared mod files (Settings > Downloads): mods and resource/shader packs that several packs use
// are kept once on disk and hard-linked into each (see Util/SharedFiles.h). This runs the
// store's housekeeping jobs on workers and keeps their latest result for the page. UI thread only.
class SharedFilesService {
public:
	SharedFilesService(const Runtime& runtime, SettingsStore& settings, PackLibrary& library);

	// Applies the "share mod files" setting to the store, and cleans out files no pack uses any
	// more (after uninstalls outside this run, too). Call once at startup.
	void Start();
	// Re-applies the setting after the player changed it.
	void ApplySetting();

	// A summary of the store ("12 file(s) stored ..."), empty until counted.
	const std::string& Stats() const { return m_stats; }
	bool Busy() const { return m_busy; }
	// Progress, or the last share job's result.
	const std::string& Status() const { return m_status; }

	// Counts the store's files on a worker.
	void Count();
	// Replaces identical files in every installed game folder and server with links to the store.
	void ShareExisting();

private:
	Runtime m_rt;
	SettingsStore& m_settings;
	PackLibrary& m_library;
	std::string m_stats;
	std::string m_status;
	bool m_busy = false;
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};

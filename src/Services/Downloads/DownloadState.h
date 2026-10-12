#pragma once

#include "Model/Modpack.h"
#include "Storage/InstallPlan.h"

#include <cstdint>
#include <string>
#include <vector>

// One download or install, as the Downloads page and the pack cards show it.
// Files are keyed by their URL; whole-pack installs by "pack:<pack id>@<version key>".
struct DownloadState {
	std::string fileName;
	Modpack pack; // pack the file belongs to, for the Downloads page (name, icon)
	double total = 0.0;
	double now = 0.0;
	bool active = false;
	std::string error;
	std::string outPath;
	bool installed = false;
	bool installing = false;
	double installProgress = 0.0;
	std::string installError;

	// Whole-pack installs (sources that install from a plan, e.g. CurseForge). These are
	// keyed "pack:<pack id>"; active/error/installed above describe the whole install.
	bool planInstall = false;
	std::string stage;            // "Resolving files", "Downloading files", ...
	std::uint64_t stageDone = 0;
	std::uint64_t stageTotal = 0;
	std::vector<PlannedFile> blocked; // files the user has to download by hand
	std::vector<std::string> notes;
};

// Program start and stop. Everything else lives in App/ (the window and the main loop),
// Services/ (what the launcher does) and UI/ (what it shows).
#include "App/Application.h"
#include "Platform/Platform.h"
#include "Util/DebugTrace.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const fs::path kIndexRelativePath = fs::path("resources") / "index.json";

// Finds the folder holding resources/index.json: the working directory first, then the
// executable's folder and each of its parents (so launching build/Debug/MC_Launcher_Premium.exe
// directly still finds the repo root). Empty if none is found.
fs::path FindDataRoot()
{
	std::error_code ec;
	const fs::path cwd = fs::current_path(ec);
	if (!ec && fs::exists(cwd / kIndexRelativePath, ec))
		return cwd;
	for (fs::path dir = Platform::ExecutableDir(); !dir.empty(); dir = dir.parent_path()) {
		if (fs::exists(dir / kIndexRelativePath, ec))
			return dir;
		if (dir == dir.parent_path())
			break;
	}
	return {};
}

}

int main(int argc, char** argv)
{
	// Release builds are GUI-subsystem apps with no console; reuse the terminal that started us, if any.
	Platform::AttachParentConsole();
	// The debug harness (--debug[=channels] or MCLP_DEBUG) traces to the console; off by default.
	std::vector<std::string> debugWarnings;
	const unsigned debugChannels = Debug::ConfigureFromEnvironment(argc, argv, debugWarnings);
	for (const std::string& warning : debugWarnings)
		std::cerr << warning << "\n";
	// --offline or MCLP_OFFLINE=1: no network requests at all, for launches that don't need the APIs.
	const bool offline = Debug::OfflineRequested(argc, argv);
	// Run from the folder that holds resources/, so the index, app.log, downloads/ and installs/
	// all resolve the same way however the app is launched.
	const fs::path dataRoot = FindDataRoot();
	std::error_code chdirError;
	if (!dataRoot.empty())
		fs::current_path(dataRoot, chdirError);
	std::ofstream logFile("app.log", std::ios::app);
	// With the log channel on, app.log lines also show in the console.
	Debug::LogTee logTee(logFile.rdbuf());
	std::ostream teedLog(&logTee);
	std::ostream& log = Debug::Enabled(Debug::kLog) ? teedLog : static_cast<std::ostream&>(logFile);
	try {
		if (debugChannels)
			Debug::Write(Debug::kLog, "Debug harness on: " + Debug::ChannelNames(debugChannels));
		log << "Starting app\n";
		if (offline)
			log << "Offline mode: network requests are disabled\n";
		if (dataRoot.empty())
			log << "Could not find " << kIndexRelativePath.string() << " near the working directory or executable\n";
		else
			log << "Data folder: " << dataRoot.string() << "\n";
		return Application(dataRoot, log, offline).Run();
	}
	catch (const std::exception& ex) {
		log << "Unhandled exception: " << ex.what() << "\n";
		logFile.close();
		throw;
	}
	catch (...) {
		log << "Unhandled unknown exception\n";
		logFile.close();
		throw;
	}
}

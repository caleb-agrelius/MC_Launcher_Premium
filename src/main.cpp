// Program start and stop. Everything else lives in App/ (the window and the main loop),
// Services/ (what the launcher does) and UI/ (what it shows).
#include "App/Application.h"
#include "Platform/Platform.h"

#include <exception>
#include <filesystem>
#include <fstream>

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

int main()
{
	// Run from the folder that holds resources/, so the index, app.log, downloads/ and installs/
	// all resolve the same way however the app is launched.
	const fs::path dataRoot = FindDataRoot();
	std::error_code chdirError;
	if (!dataRoot.empty())
		fs::current_path(dataRoot, chdirError);
	std::ofstream log("app.log", std::ios::app);
	try {
		log << "Starting app\n";
		if (dataRoot.empty())
			log << "Could not find " << kIndexRelativePath.string() << " near the working directory or executable\n";
		else
			log << "Data folder: " << dataRoot.string() << "\n";
		return Application(dataRoot, log).Run();
	}
	catch (const std::exception& ex) {
		log << "Unhandled exception: " << ex.what() << "\n";
		log.close();
		throw;
	}
	catch (...) {
		log << "Unhandled unknown exception\n";
		log.close();
		throw;
	}
}

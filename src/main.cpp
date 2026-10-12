#include "imgui.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "Game/Launcher.h"
#include "Game/PackInstance.h"
#include "Game/ServerSetup.h"
#include "Util/Process.h"
#include "Util/Text.h"
#include "imgui_stdlib.h"
#include "Model/Modpack.h"
#include "Platform.h"
#include "Network/HttpClient.h"
#include "Network/CurseForge.h"
#include "Network/MicrosoftAuth.h"
#include "Network/Modrinth.h"
#include "Network/PackSources.h"
#include "Storage/Account.h"
#include "Storage/InstallPlan.h"
#include "Storage/Installer.h"
#include "Storage/Library.h"
#include "Storage/Settings.h"
#include "UI/MobModel.h"
#include "UI/Mobs.h"
#include "UI/ImageCache.h"
#include "UI/SettingsPage.h"
#include "UI/Theme.h"
#include "UI/Toasts.h"
#include "UI/Widgets.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace fs = std::filesystem;
using Theme::S;

static const fs::path kIndexRelativePath = fs::path("resources") / "index.json";
static const char* kAppVersion = "0.1.0";
static const char* kSettingsFile = "settings.json";
static const char* kLibraryFile = "library.json";
static const char* kDownloadsFile = "downloads.json";
static const char* kAccountFile = "account.dat";
static const char* kGameRoot = "minecraft";      // shared Minecraft versions, libraries, assets, Java
static const char* kInstancesRoot = "instances"; // one game directory per pack
static const char* kServersRoot = "servers";     // one dedicated server per pack
static constexpr float kUiScaleStep = 0.1f;
static constexpr double kSearchDelaySeconds = 0.4; // pause after typing before searching

static fs::path GetExecutableDir(const char* argv0)
{
	std::error_code ec;
#ifdef _WIN32
	wchar_t buffer[MAX_PATH * 4];
	const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
	if (length > 0 && length < std::size(buffer))
		return fs::path(buffer, buffer + length).parent_path();
#else
	const fs::path self = fs::read_symlink("/proc/self/exe", ec);
	if (!ec)
		return self.parent_path();
#endif
	if (argv0 != nullptr)
		return fs::absolute(argv0, ec).parent_path();
	return {};
}

// Finds the folder holding resources/index.json: the working directory first,
// then the executable's folder and each of its parents (so launching
// build/Debug/MC_Launcher_Premium.exe directly still finds the repo root).
// Returns an empty path if none is found.
static fs::path FindDataRoot(const char* argv0)
{
	std::error_code ec;
	const fs::path cwd = fs::current_path(ec);
	if (!ec && fs::exists(cwd / kIndexRelativePath, ec))
		return cwd;

	for (fs::path dir = GetExecutableDir(argv0); !dir.empty(); dir = dir.parent_path()) {
		if (fs::exists(dir / kIndexRelativePath, ec))
			return dir;
		if (dir == dir.parent_path())
			break;
	}
	return {};
}

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

enum class Page {
	Discover,
	Library,
	Downloads,
	Servers,
	Settings
};

// A pack's dedicated server (Servers page). UI thread only, except the setup fields,
// which the setup worker updates under serversMutex.
struct ServerState {
	bool loaded = false; // the fields below have been read from disk
	bool ready = false;  // server files are installed (.mclp-server.json exists)
	ServerInfo info;
	ServerProperties props;
	bool eula = false;
	int memoryMb = 4096;
	std::unique_ptr<ServerProcess> process;
	bool running = false;        // as of last frame, to notice when the server stops
	double stopRequestedAt = -1.0;
	char command[256] = "";
	bool hostIpCustom = false; // Host IP: typing an address instead of picking one
	std::string hostIpEdit;    // the address being typed, applied once it's valid
	std::uint64_t seenLines = 0; // console lines when last drawn, to auto-scroll on new output
	std::vector<ServerMod> mods; // servers/<id>/mods, for turning mods on and off
	bool modsLoaded = false;
	std::string modFilter;
	std::string modError;
	bool dockerBusy = false;   // a "docker compose" command is running
	std::string dockerMessage; // the last Docker action's result
	bool dockerFailed = false;
	// Setup, written by the worker under serversMutex.
	bool busy = false;
	bool deleting = false; // busy deleting the server folder, which can't be cancelled
	std::string step;
	double progress = -1.0;
	std::string error;
	std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
	// The pack version Create/Update server files uses; unset = the version installed in My Library.
	bool hasVersion = false;
	PackVersionInfo version;
};

// True when id can name a folder under installs/ or servers/ that is safe to delete.
static bool IsPlainFolderName(const std::string& id)
{
	return !id.empty() && id != "." && id != ".." && id.find_first_of("/\\:") == std::string::npos;
}

// Per-server memory, kept next to the server so it survives reinstalls of the launcher.
static int LoadServerMemory(const fs::path& serverDir, int fallback)
{
	std::ifstream in(serverDir / ".mclp-memory");
	int memory = 0;
	return (in >> memory && memory >= 1024) ? memory : fallback;
}

static void SaveServerMemory(const fs::path& serverDir, int memoryMb)
{
	std::error_code ec;
	fs::create_directories(serverDir, ec);
	std::ofstream(serverDir / ".mclp-memory", std::ios::trunc) << memoryMb << "\n";
}

// One source's query, filters and results for Discover's combined search.
struct SourceView {
	PackSource source = PackSource::Modrinth;
	SourceCapabilities caps;
	char search[128] = "";
	char gameVersion[32] = "";
	int loader = 0; // 0 = any, else caps.loaders[loader - 1]
	int sort = 0;   // index into caps.sorts
	std::vector<Modpack> packs;
	int selected = -1;
	// Typing schedules a search; it runs once the input has been unchanged for
	// kSearchDelaySeconds. The first visit runs the default (empty) search.
	bool searchPending = true;
	double lastSearchEdit = -1.0;
	bool searching = false;
	int searchGeneration = 0; // results from older searches are ignored
	double resultsAt = -1000.0; // ImGui::GetTime() when the current results arrived
	std::string searchStatus;
	bool searchFailed = false;
	std::uint64_t totalHits = 0;
	int revision = 0; // bumped whenever packs is replaced, so Discover re-merges
};

// Discover's combined search: one query and set of filters, sent to every selected
// source at once. Each source's results stay in its SourceView; the list shown is
// merged from them (see `merged`).
struct DiscoverState {
	char search[128] = "";
	char gameVersion[32] = "";
	std::string loader; // "" = any, else "forge", "neoforge", "fabric" or "quilt"
	std::set<PackSource> sources; // the source filter; empty = every source
	// With one source: that source's sort index. With several: 0 best match
	// (the sources' results interleaved by rank), 1 most downloads, 2 name.
	int sort = 0;
	bool searchPending = true;
	double lastSearchEdit = -1.0;
	std::vector<PackSource> activeSources; // what the last search was sent to
	std::vector<std::string> skippedSources; // and why the others weren't, for the status line
	std::vector<std::pair<PackSource, std::size_t>> merged; // (source view, index into its packs)
	std::string mergedKey; // the sort and source revisions merged was built from
	std::string selectedId;
};

// Each source's brand color, for its filter chip and result tags.
static ImVec4 SourceColor(PackSource source)
{
	switch (source) {
	case PackSource::Modrinth: return ImVec4(0.11f, 0.85f, 0.42f, 1.0f);
	case PackSource::CurseForge: return ImVec4(0.95f, 0.39f, 0.21f, 1.0f);
	case PackSource::FTB: return ImVec4(0.86f, 0.24f, 0.26f, 1.0f);
	case PackSource::Technic: return ImVec4(0.25f, 0.6f, 0.95f, 1.0f);
	case PackSource::ATLauncher: return ImVec4(0.6f, 0.82f, 0.27f, 1.0f);
	default: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
	}
}

static const char* const kLoaderKeys[] = { "forge", "neoforge", "fabric", "quilt" };
static const char* const kCombinedSorts[] = { "Best match", "Most downloads", "Name" };

static const char* LoaderLabel(const char* loader)
{
	const std::string key = loader;
	if (key == "forge") return "Forge";
	if (key == "neoforge") return "NeoForge";
	if (key == "fabric") return "Fabric";
	if (key == "quilt") return "Quilt";
	return loader;
}

static std::string FormatFileSize(std::uint64_t bytes)
{
	const char* units[] = { "B", "KB", "MB", "GB" };
	double size = static_cast<double>(bytes);
	std::size_t unit = 0;
	while (size >= 1024.0 && unit < 3) {
		size /= 1024.0;
		++unit;
	}
	char formatted[48];
	std::snprintf(formatted, sizeof(formatted), unit == 0 ? "%.0f %s" : "%.1f %s", size, units[unit]);
	return formatted;
}

// Formats large counts compactly: 950, 12.3K, 18.4M.
static std::string FormatCount(std::uint64_t count)
{
	char formatted[32];
	if (count >= 1000000)
		std::snprintf(formatted, sizeof(formatted), "%.1fM", count / 1000000.0);
	else if (count >= 1000)
		std::snprintf(formatted, sizeof(formatted), "%.1fK", count / 1000.0);
	else
		std::snprintf(formatted, sizeof(formatted), "%llu", static_cast<unsigned long long>(count));
	return formatted;
}

static std::string ToLower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

static bool ContainsIgnoreCase(const std::string& haystack, const std::string& lowerNeedle)
{
	return ToLower(haystack).find(lowerNeedle) != std::string::npos;
}

// A background task plus a flag it sets when finished, so the UI loop can join
// and drop completed threads instead of keeping them until exit.
struct Worker {
	std::thread thread;
	std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
};

static void StartWorker(std::vector<Worker>& workers, std::function<void()> task)
{
	Worker worker;
	worker.thread = std::thread([task = std::move(task), done = worker.done]() {
		task();
		// Close this thread's connections before reporting done, so the UI thread's
		// join in ReapFinishedWorkers doesn't wait on curl cleanup.
		HttpClient::ReleaseThreadHandle();
		done->store(true);
	});
	workers.push_back(std::move(worker));
}

static void ReapFinishedWorkers(std::vector<Worker>& workers)
{
	for (auto it = workers.begin(); it != workers.end();) {
		if (it->done->load()) {
			it->thread.join();
			it = workers.erase(it);
		} else {
			++it;
		}
	}
}

// Work handed from background threads to the UI thread. Drain() runs it at the
// start of a frame, so UI state (pack list, selection) is only touched on the UI thread.
struct UiQueue {
	std::mutex mutex;
	std::vector<std::function<void()>> tasks;

	void Post(std::function<void()> task)
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back(std::move(task));
	}

	void Drain()
	{
		std::vector<std::function<void()>> pending;
		{
			std::lock_guard<std::mutex> lock(mutex);
			pending.swap(tasks);
		}
		for (auto& task : pending)
			task();
	}
};

// Matches the native title bar to the theme: dark or light mode on Windows 10/11, and the
// theme's background and text colors on Windows 11 (older versions ignore those).
static void ApplyTitleBarColors(GLFWwindow* window, const Theme::Palette& palette)
{
#ifdef _WIN32
	const HWND hwnd = glfwGetWin32Window(window);
	const BOOL dark = palette.light ? FALSE : TRUE;
	DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
	auto toColorRef = [](const ImVec4& color) {
		return RGB(static_cast<int>(color.x * 255.0f + 0.5f), static_cast<int>(color.y * 255.0f + 0.5f),
			static_cast<int>(color.z * 255.0f + 0.5f));
	};
	const COLORREF caption = toColorRef(palette.windowBg);
	const COLORREF text = toColorRef(palette.text);
	DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
	DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
#else
	(void)window;
	(void)palette;
#endif
}

// Opens a web page in the default browser.
static void OpenUrl(const std::string& url)
{
#ifdef _WIN32
	ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
	(void)url;
#endif
}

// True when a saved window's title bar would be on a connected monitor, so a window
// saved on a monitor that's been unplugged doesn't open off screen.
static bool WindowFitsOnAMonitor(const WindowPlacement& placement)
{
	int count = 0;
	GLFWmonitor** monitors = glfwGetMonitors(&count);
	for (int i = 0; i < count; ++i) {
		int x = 0, y = 0, width = 0, height = 0;
		glfwGetMonitorWorkarea(monitors[i], &x, &y, &width, &height);
		const int titleX = placement.x + std::min(placement.width / 2, 100);
		const int titleY = placement.y + 10;
		if (titleX >= x && titleX < x + width && titleY >= y && titleY < y + height &&
			placement.width <= width + 40 && placement.height <= height + 40)
			return true;
	}
	return false;
}

// Rounds to the nearest 5% and keeps the scale within the supported range.
static float NormalizeUiScale(float scale)
{
	return std::clamp(std::round(scale * 20.0f) / 20.0f, kMinUiScale, kMaxUiScale);
}

// Moves the cursor so a button with this label ends at the right edge of the content region.
static void AlignRightForButton(const char* label)
{
	const float width = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
	if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x <= ImGui::GetWindowPos().x + x)
		ImGui::SameLine(x);
	else
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x));
}

// Moves the cursor so a row of buttons with these labels ends at the right edge.
static void AlignRightForButtons(const std::vector<const char*>& labels)
{
	if (labels.empty())
		return;
	const ImGuiStyle& style = ImGui::GetStyle();
	float width = 0.0f;
	for (const char* label : labels)
		width += ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f;
	width += style.ItemSpacing.x * static_cast<float>(labels.size() - 1);
	const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
	if (ImGui::GetItemRectMax().x + style.ItemSpacing.x <= ImGui::GetWindowPos().x + x)
		ImGui::SameLine(x);
	else
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x));
}

using Widgets::BeginCard;
using Widgets::EndCard;

// Draws a pack icon at the cursor and leaves the cursor beside it, ready for SameLine text.
static void PackIconItem(const Modpack& pack, ImageCache& images, float size)
{
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(size, size));
	Widgets::DrawPackIcon(ImGui::GetWindowDrawList(), images.Get(pack.iconUrl), pack, pos, size, S(8.0f));
}

int main(int argc, char** argv)
{
	(void)argc;
	// Run from the folder that holds resources/, so the index, app.log,
	// downloads/ and installs/ all resolve the same way however the app is launched.
	const fs::path dataRoot = FindDataRoot(argc > 0 ? argv[0] : nullptr);
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
		if (!glfwInit()) {
			log << "glfwInit failed\n";
			return -1;
		}

		Settings settings = LoadSettings(kSettingsFile);

		const char* glsl_version = "#version 130";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		// The last window size is restored as-is (it's in real pixels). The first time, the
		// default size is in logical pixels, so it isn't tiny on high-DPI (e.g. 4K at 150%) monitors.
		const bool restoreWindow = settings.window.saved && WindowFitsOnAMonitor(settings.window);
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, restoreWindow ? GLFW_FALSE : GLFW_TRUE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // shown once it's in place

		GLFWwindow* window = restoreWindow
			? glfwCreateWindow(settings.window.width, settings.window.height, "MC Launcher Premium", nullptr, nullptr)
			: glfwCreateWindow(1360, 840, "MC Launcher Premium", nullptr, nullptr);
		if (window == nullptr) {
			log << "glfwCreateWindow failed\n";
			glfwTerminate();
			return -1;
		}
		WindowPlacement lastPlacement;
		if (restoreWindow) {
			glfwSetWindowPos(window, settings.window.x, settings.window.y);
		}
		glfwGetWindowPos(window, &lastPlacement.x, &lastPlacement.y);
		glfwGetWindowSize(window, &lastPlacement.width, &lastPlacement.height);
		if (restoreWindow) {
			if (settings.window.maximized)
				glfwMaximizeWindow(window);
		}
		glfwShowWindow(window);
		// The size and position to save: the last ones seen while not maximized or minimized.
		glfwMakeContextCurrent(window);
		glfwSwapInterval(1);

		if (!gladLoadGL()) {
			fprintf(stderr, "Failed to initialize GL loader\n");
			glfwDestroyWindow(window);
			glfwTerminate();
			return -1;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		// UI scale = monitor DPI scale (from Windows display settings) x the user's UI scale setting.
		// Theme, scale and DPI are re-applied at the start of a frame whenever one of them changes.
		bool settingsDirty = false; // saved once no widget is being edited
		float dpiScale = 1.0f;
		glfwGetWindowContentScale(window, &dpiScale, nullptr);
		float appliedUiScale = settings.uiScale;
		Theme::SetCustomThemes(settings.customThemes);
		std::size_t appliedTheme = Theme::FindTheme(settings.theme);
		int appliedThemeRevision = Theme::Revision();
		Theme::LoadFonts();
		Theme::Apply(appliedTheme, dpiScale, settings.uiScale);
		ApplyTitleBarColors(window, Theme::C());
		Widgets::SetReduceMotion(settings.reduceMotion);
		glfwSetWindowSizeLimits(window, static_cast<int>(S(900.0f)), static_cast<int>(S(600.0f)), GLFW_DONT_CARE, GLFW_DONT_CARE);
		log << "DPI scale " << dpiScale << ", UI scale " << settings.uiScale << ", theme " << Theme::Themes()[appliedTheme].id << "\n";

		if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
			log << "ImGui GLFW backend initialization failed\n";
			ImGui::DestroyContext();
			glfwDestroyWindow(window);
			glfwTerminate();
			return -1;
		}
		if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
			log << "ImGui OpenGL backend initialization failed\n";
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			glfwDestroyWindow(window);
			glfwTerminate();
			return -1;
		}

		// The local index is the offline fallback; Discover normally shows Modrinth results.
		const std::vector<Modpack> localPacks = LoadIndex(kIndexRelativePath.string());
		log << "Loaded " << localPacks.size() << " local modpack(s)\n";
		log.flush();
		// One view per online source. std::map keeps references stable,
		// so background tasks can hold a SourceView& safely.
		std::map<PackSource, SourceView> views;
		for (PackSource source : OnlineSources()) {
			SourceView& view = views[source];
			view.source = source;
			view.caps = GetSourceCapabilities(source);
		}
		DiscoverState discover;
		for (const std::string& key : settings.discoverSources) {
			const PackSource source = PackSourceFromKey(key);
			if (views.count(source))
				discover.sources.insert(source);
		}
		Page page = Page::Discover;
		bool focusSearch = false; // Ctrl+F
		// Entrance animations: when the shown page and the pack in the
		// details pane last changed.
		int shownPageKey = -1;
		double pageShownAt = 0.0;
		std::string detailsPackId;
		double detailsShownAt = 0.0;
		HttpClient http;
		ImageCache images(http);
		// The sidebar mascot: a random mob each launch; right-click it for another.
		MobModel mascot;
		Mob mascotMob = Mob::Count;
		std::string mascotTooltip;
		auto rollMascot = [&]() {
			for (int attempt = 0; attempt < 3; ++attempt) {
				const Mob mob = RandomMob(mascotMob);
				mascotMob = mob; // a failed mob isn't picked again on the next attempt
				std::string error;
				bool loaded = false;
				if (mob == Mob::Fox) {
					loaded = !dataRoot.empty() &&
						mascot.Load((dataRoot / "src" / "assets" / "minecraft_fox.glb").string(), &error);
				} else {
					std::vector<MobBox> boxes;
					std::array<float, 3> pivot{};
					loaded = BuildMobBoxes(mob, boxes, pivot) && mascot.LoadBoxes(boxes, pivot, &error);
				}
				if (loaded) {
					mascot.SetReduceMotion(settings.reduceMotion);
					mascot.SetIdleBob(MobIdleBob(mob));
					mascot.SetSizeFactor(MobSizeFactor(mob));
					mascotTooltip = std::string(MobName(mob)) + "\nClick to make it hop. Right-click for another mob.";
					log << "Mascot: " << MobName(mob) << "\n";
					return;
				}
				log << "Mascot " << MobName(mob) << " unavailable: " << (error.empty() ? "model file not found" : error) << "\n";
			}
		};
		rollMascot();
		std::map<std::string, DownloadState> downloads;
		std::mutex downloadsMutex;
		std::vector<Worker> workers;
		std::atomic<bool> shuttingDown{ false };
		UiQueue uiQueue;
		fs::create_directories("downloads");
		fs::create_directories("installs");

		// Per-pack version lookups for online results, keyed by pack id (unique across sources).
		std::set<std::string> filesLoading;
		std::map<std::string, std::string> filesErrors;
		// Full-size banner lookups, keyed by pack id.
		std::set<std::string> bannersLoading;
		// Mod lists for Discover's Mods tab, keyed by ModsKey (pack id and version).
		std::map<std::string, PackModList> packMods;
		std::set<std::string> modsLoading;
		std::map<std::string, std::string> modsErrors;
		std::string modsFilter;
		auto modsKey = [](const Modpack& pack) { return pack.id + "@" + pack.sourceVersionId; };
		// Full descriptions for Discover's Description tab (plain text), keyed by pack id.
		std::map<std::string, std::string> packDescriptions;
		std::set<std::string> descriptionsLoading;
		std::map<std::string, std::string> descriptionErrors;

		std::error_code cwdError;
		SettingsPageContext settingsContext;
		settingsContext.appVersion = kAppVersion;
		settingsContext.dataFolder = fs::current_path(cwdError).string();
		settingsContext.systemMemoryMb = Platform::SystemMemoryMb();
		settingsContext.dpiScale = dpiScale;
		settingsContext.curseForgeEnvKeySet = !CurseForgeEnvApiKey().empty();
		SetCurseForgeApiKey(settings.curseForgeApiKey);
		// Installed packs and their versions (UI thread only). Saved to library.json so My
		// Library survives restarts. Versions whose install folder is gone are dropped, and so
		// are packs left with none.
		Library libraryPacks = LoadPackLibrary(kLibraryFile);
		for (auto it = libraryPacks.begin(); it != libraryPacks.end();) {
			Modpack& pack = it->second;
			std::vector<std::string> missing;
			for (const InstalledVersion& v : pack.installedVersions) {
				std::error_code existsError;
				if (!fs::is_directory(v.installDir, existsError))
					missing.push_back(v.key);
			}
			bool keep = !pack.installedVersions.empty();
			for (const std::string& key : missing)
				keep = RemoveInstalledVersion(pack, key);
			if (keep)
				++it;
			else
				it = libraryPacks.erase(it);
		}
		log << "Loaded " << libraryPacks.size() << " library pack(s)\n";

		// A library pack's active version: its install folder (the unzipped .mrpack, or
		// pack.json + minecraft/) and its game folder. Packs not in the library fall back to
		// the original single-version layout.
		auto activeVersionOf = [&](const Modpack& pack) -> const InstalledVersion* {
			const auto it = libraryPacks.find(pack.id);
			return it != libraryPacks.end() ? ActiveInstalledVersion(it->second) : nullptr;
		};
		auto installDir = [&](const Modpack& pack) -> fs::path {
			const InstalledVersion* v = activeVersionOf(pack);
			return v ? fs::u8path(v->installDir) : fs::path("installs") / pack.id;
		};
		auto gameDir = [&](const Modpack& pack) -> fs::path {
			if (const InstalledVersion* v = activeVersionOf(pack))
				return fs::u8path(v->gameDir);
			return GetSourceCapabilities(pack.source).installsPacks ? fs::path("installs") / pack.id / "minecraft"
				: fs::path(kInstancesRoot) / pack.id;
		};

		// Finished downloads from earlier sessions, so Discover offers "Install files" again.
		// Entries whose file was deleted are skipped. Workers aren't running yet, so no lock.
		for (SavedDownload& saved : LoadSavedDownloads(kDownloadsFile)) {
			std::error_code existsError;
			if (!fs::is_regular_file(saved.outPath, existsError))
				continue;
			DownloadState state;
			state.fileName = std::move(saved.fileName);
			state.outPath = std::move(saved.outPath);
			{
				const auto entry = libraryPacks.find(saved.pack.id);
				state.installed = entry != libraryPacks.end() && FindInstalledVersion(entry->second, VersionKey(saved.pack));
			}
			state.pack = std::move(saved.pack);
			downloads[saved.url] = std::move(state);
		}
		log << "Loaded " << downloads.size() << " saved download(s)\n";

		// Writes every finished download to downloads.json. UI thread only.
		auto saveDownloads = [&]() {
			std::vector<SavedDownload> saved;
			{
				std::lock_guard<std::mutex> lock(downloadsMutex);
				for (const auto& [url, state] : downloads) {
					if (!state.outPath.empty())
						saved.push_back({ url, state.fileName, state.outPath, state.pack });
				}
			}
			if (!SaveDownloads(kDownloadsFile, saved))
				log << "Could not save " << kDownloadsFile << "\n";
		};

		// Told about every finished version install, ok or not, after the library has been
		// updated. Set further down by pack updates. UI thread only.
		std::function<void(const Modpack& pack, bool ok)> onVersionInstalled;

		// Extracts a downloaded file into installs/<pack id>/ and, on success, adds
		// the pack to My Library. Used by Discover and the Downloads page.
		auto startInstall = [&](const std::string& url, const Modpack& pack, const std::string& zipPath) {
			{
				std::lock_guard<std::mutex> lock(downloadsMutex);
				DownloadState& current = downloads[url];
				current.installing = true;
				current.installProgress = 0.0;
				current.installError.clear();
			}
			// Each version gets its own folder, so versions can be installed side by side.
			const InstalledVersion version = PlanInstalledVersion(libraryPacks, pack, false);
			const std::string targetDir = version.installDir;
			StartWorker(workers, [zipPath, targetDir, &downloads, &downloadsMutex, &shuttingDown, &uiQueue,
				&libraryPacks, &log, &onVersionInstalled, url, pack, version]() {
				std::string error;
				// Start from an empty folder, so another version's overrides don't linger.
				std::error_code clearError;
				fs::remove_all(targetDir, clearError);
				auto progress = [&](int total, int now) {
					std::lock_guard<std::mutex> lock(downloadsMutex);
					DownloadState& current = downloads[url];
					if (total > 0)
						current.installProgress = static_cast<double>(now) / static_cast<double>(total);
				};
				const bool ok = InstallZip(zipPath, targetDir, progress, &error, &shuttingDown);
				{
					std::lock_guard<std::mutex> lock(downloadsMutex);
					DownloadState& current = downloads[url];
					current.installing = false;
					if (ok)
						current.installed = true;
					else
						current.installError = error;
				}
				if (ok) {
					uiQueue.Post([&libraryPacks, &log, &onVersionInstalled, pack, version]() {
						AddInstalledVersion(libraryPacks, pack, version);
						if (!SavePackLibrary(kLibraryFile, libraryPacks))
							log << "Could not save " << kLibraryFile << "\n";
						Toasts::Push(Toasts::Kind::Success, "Files extracted", pack.name + " " + pack.version + " is in your library.");
						if (onVersionInstalled)
							onVersionInstalled(pack, true);
					});
				} else {
					uiQueue.Post([&onVersionInstalled, pack, error, cancelled = shuttingDown.load()]() {
						if (!cancelled)
							Toasts::Push(Toasts::Kind::Error, "Extraction failed", pack.name + ": " + error);
						if (onVersionInstalled)
							onVersionInstalled(pack, false);
					});
				}
			});
		};

		// Downloads one of a pack's files into downloads/<pack id>/, then extracts it with
		// startInstall. Discover's one-step Install and pack updates use it.
		auto startDownloadInstall = [&](const Modpack& pack, const PackFile& file) {
			DownloadState started;
			started.fileName = file.name;
			started.pack = pack;
			started.active = true;
			{
				std::lock_guard<std::mutex> lock(downloadsMutex);
				downloads[file.url] = started;
			}
			const std::string url = file.url;
			// Per-pack folder so same-named files from different packs don't collide.
			const fs::path packDownloadDir = fs::path("downloads") / pack.id;
			const std::string outputPath = (packDownloadDir / file.name).string();
			StartWorker(workers, [&http, &downloads, &downloadsMutex, &shuttingDown, &uiQueue, &saveDownloads,
				&startInstall, &onVersionInstalled, url, packDownloadDir, outputPath, pack]() {
				std::string error;
				std::error_code dirError;
				fs::create_directories(packDownloadDir, dirError);
				auto progress = [&](double total, double now) {
					std::lock_guard<std::mutex> lock(downloadsMutex);
					DownloadState& current = downloads[url];
					current.total = total;
					current.now = now;
				};
				const bool ok = http.DownloadToFile(url, outputPath, progress, &error, &shuttingDown);
				{
					std::lock_guard<std::mutex> lock(downloadsMutex);
					DownloadState& current = downloads[url];
					current.active = false;
					if (ok) {
						current.outPath = outputPath;
						current.installing = !shuttingDown.load(); // extraction starts next frame
					} else {
						current.error = error;
					}
				}
				if (ok)
					uiQueue.Post([&saveDownloads]() { saveDownloads(); });
				if (shuttingDown.load())
					return; // the file stays downloaded; Install files on Downloads extracts it later
				if (ok)
					uiQueue.Post([&startInstall, url, pack, outputPath]() { startInstall(url, pack, outputPath); });
				else
					uiQueue.Post([&onVersionInstalled, pack, error]() {
						Toasts::Push(Toasts::Kind::Error, "Download failed", pack.name + ": " + error);
						if (onVersionInstalled)
							onVersionInstalled(pack, false);
					});
			});
		};

		// ---- Account (UI thread only) ----
		AccountList accounts = LoadAccounts(kAccountFile);
		Account account = accounts.Active(); // copy of the active account, kept in step by saveAccounts
		log << "Loaded " << accounts.accounts.size() << " account(s)\n";
		enum class SignIn { Idle, Starting };
		SignIn signIn = SignIn::Idle;
		std::string signInError;
		auto signInCancel = std::make_shared<std::atomic<bool>>(false);
		char offlineName[17] = "";
		bool openOfflinePopup = false;

		auto saveAccounts = [&]() {
			account = accounts.Active();
			std::error_code removeError;
			if (accounts.accounts.empty())
				fs::remove(kAccountFile, removeError);
			else if (!SaveAccounts(kAccountFile, accounts))
				log << "Could not save " << kAccountFile << "\n";
		};
		// A new sign-in (or offline name) becomes the active account.
		auto addAccount = [&](const Account& newAccount) {
			accounts.AddOrReplace(newAccount);
			saveAccounts();
		};
		// Fresh tokens for a saved account; it keeps its place in the list.
		auto updateAccount = [&](const Account& refreshed) {
			accounts.Update(refreshed);
			saveAccounts();
		};

		auto startMicrosoftSignIn = [&]() {
			signIn = SignIn::Starting;
			signInError.clear();
			signInCancel = std::make_shared<std::atomic<bool>>(false);
			const std::string clientId = settings.microsoftClientId;
			StartWorker(workers, [&http, &uiQueue, &signIn, &signInError, &addAccount, &log,
				clientId, cancel = signInCancel]() {
				std::string error;
				Account signedIn;
				const bool ok = SignInWithMicrosoft(http, clientId, signedIn, &error, cancel.get());
				uiQueue.Post([&signIn, &signInError, &addAccount, &log, ok, signedIn, error, cancel]() {
					signIn = SignIn::Idle;
					if (ok) {
						addAccount(signedIn);
						log << "Signed in as " << signedIn.name << "\n";
					} else if (!cancel->load()) {
						signInError = error;
						log << "Microsoft sign-in failed: " << error << "\n";
					}
				});
			});
		};

		// ---- Playing (state shared with play workers) ----
		struct PlayState {
			bool busy = false;    // preparing or running
			bool playing = false; // Minecraft has started
			std::string step;
			double progress = -1.0;
			std::string error;
			std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
			std::shared_ptr<std::atomic<bool>> stop = std::make_shared<std::atomic<bool>>(false); // force-close the game
		};
		std::map<std::string, PlayState> plays;
		std::mutex playsMutex;
		std::string stopConfirmPackId; // pack whose "Stop game" confirmation is open

		// ---- Servers ----
		std::map<std::string, ServerState> servers; // keyed by pack id; entries only added on the UI thread
		std::mutex serversMutex;
		std::string serverPackId; // selected on the Servers page
		std::string lanAddress = Platform::LocalNetworkAddress();
		// This PC's addresses for Servers > Host IP. Re-read when the page opens, since VPNs
		// (Tailscale, ZeroTier, Radmin) add and remove adapters.
		std::vector<Platform::NetworkAddress> hostAddresses = Platform::LocalNetworkAddresses();

		auto startServerSetup = [&](const Modpack& pack) {
			auto cancel = std::make_shared<std::atomic<bool>>(false);
			// A version other than the installed one: setup downloads that version's .mrpack.
			PackFile otherVersion;
			fs::path otherInstalledDir; // another installed version's folder
			{
				std::lock_guard<std::mutex> lock(serversMutex);
				ServerState& state = servers[pack.id];
				if (state.hasVersion) {
					// Another installed version is used as it is; any other one is downloaded.
					Modpack wanted = pack;
					wanted.version = state.version.versionNumber;
					wanted.sourceVersionId = state.version.sourceVersionId;
					const auto entry = libraryPacks.find(pack.id);
					const InstalledVersion* installed = entry != libraryPacks.end()
						? FindInstalledVersion(entry->second, VersionKey(wanted)) : nullptr;
					if (installed)
						otherInstalledDir = fs::absolute(fs::u8path(installed->installDir));
					else if (!state.version.files.empty())
						otherVersion = state.version.files.front();
					else {
						// Whole-pack sources have no single file to download; their servers are built
						// from an installed version, so don't quietly fall back to the active one.
						state.error = "Install " + state.version.versionNumber + " from Discover first. " +
							PackSourceName(pack.source) + " servers are built from an installed version.";
						return;
					}
				}
				state.busy = true;
				state.step = "Starting";
				state.progress = -1.0;
				state.error.clear();
				state.cancel = cancel;
			}
			ServerSetupRequest request;
			request.gameRoot = fs::absolute(kGameRoot);
			request.extractedDir = otherInstalledDir.empty() ? fs::absolute(installDir(pack)) : otherInstalledDir;
			request.serverDir = fs::absolute(fs::path(kServersRoot) / pack.id);
			request.javaPath = settings.launch.javaPath;
			// Whole-pack installs: Modrinth knows which mods don't run on servers.
			request.findClientOnly = [&http, cancel](const std::vector<std::string>& sha1s, std::set<std::string>& clientOnly) {
				return FindModrinthClientOnly(http, sha1s, clientOnly, nullptr, cancel.get());
			};
			const std::string packId = pack.id;
			const std::string packName = pack.name;
			log << "Setting up server for " << packId << "\n";
			StartWorker(workers, [&http, &uiQueue, &servers, &serversMutex, &log, request, packId, packName, cancel,
				otherVersion]() mutable {
				auto progress = [&](const std::string& step, double fraction) {
					std::lock_guard<std::mutex> lock(serversMutex);
					ServerState& state = servers.at(packId);
					state.step = step;
					state.progress = fraction;
				};
				ServerInfo info;
				std::string error;
				bool ok = true;
				try {
					if (!otherVersion.url.empty()) {
						// The chosen version's .mrpack, unpacked into servers/<id>/.mclp-pack/ (fresh each time).
						const fs::path zip = fs::absolute(fs::path("downloads") / packId / "server" / otherVersion.name);
						const fs::path unpacked = request.serverDir / ".mclp-pack";
						std::error_code ec;
						fs::create_directories(zip.parent_path(), ec);
						progress("Downloading " + otherVersion.name, -1.0);
						ok = http.DownloadToFile(otherVersion.url, zip.string(), [&](double total, double now) {
							if (total > 0.0)
								progress("Downloading " + otherVersion.name, now / total);
						}, &error, cancel.get());
						if (ok) {
							progress("Unpacking " + otherVersion.name, -1.0);
							fs::remove_all(unpacked, ec);
							ok = InstallZip(zip.string(), unpacked.string(), nullptr, &error, cancel.get());
						}
						request.extractedDir = unpacked;
					}
					if (ok)
						ok = SetupServer(http, request, info, progress, &error, cancel.get());
				} catch (const std::exception& ex) {
					error = std::string("Server setup failed: ") + ex.what();
				}
				const bool cancelled = cancel->load();
				uiQueue.Post([&servers, &serversMutex, &log, packId, packName, ok, cancelled, info, error]() {
					std::lock_guard<std::mutex> lock(serversMutex);
					ServerState& state = servers.at(packId);
					state.busy = false;
					state.error = (ok || cancelled) ? std::string() : error;
					if (ok) {
						state.ready = true;
						state.info = info;
						state.props = ReadServerProperties(fs::path(kServersRoot) / packId);
						state.eula = EulaAccepted(fs::path(kServersRoot) / packId);
						state.modsLoaded = false;
						log << "Server for " << packId << " ready: " << info.loader << " " << info.minecraftVersion << "\n";
						Toasts::Push(Toasts::Kind::Success, "Server files ready", packName + " can now be started.");
					} else if (!cancelled) {
						log << "Server setup for " << packId << " failed: " << error << "\n";
						Toasts::Push(Toasts::Kind::Error, "Server setup failed", packName + ": " + error);
					}
				});
			});
		};

		// Servers whose pack is no longer in My Library (uninstalled without deleting the
		// server), so they can still be run or deleted. Named after their folder. UI thread only.
		std::map<std::string, Modpack> orphanServers;
		auto refreshOrphanServers = [&]() {
			orphanServers.clear();
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(kServersRoot, ec)) {
				const std::string id = entry.path().filename().string();
				if (!entry.is_directory(ec) || libraryPacks.count(id) || !IsPlainFolderName(id))
					continue;
				Modpack pack;
				pack.id = id;
				pack.name = id;
				orphanServers[id] = pack;
			}
		};

		// Deletes servers/<pack id>/ on a worker. The server must be stopped and not setting up.
		auto startServerDelete = [&](const std::string& packId, const std::string& packName) {
			if (!IsPlainFolderName(packId))
				return;
			{
				std::lock_guard<std::mutex> lock(serversMutex);
				ServerState& state = servers[packId];
				state.busy = true;
				state.deleting = true;
				state.step = "Deleting server files";
				state.progress = -1.0;
				state.error.clear();
			}
			log << "Deleting server " << packId << "\n";
			StartWorker(workers, [&uiQueue, &servers, &serversMutex, &orphanServers, &log, packId, packName]() {
				std::string error;
				const bool ok = RemoveServer(fs::path(kServersRoot) / packId, &error);
				uiQueue.Post([&servers, &serversMutex, &orphanServers, &log, packId, packName, ok, error]() {
					std::lock_guard<std::mutex> lock(serversMutex);
					ServerState& state = servers.at(packId);
					if (ok) {
						state = ServerState{}; // re-read from disk (now empty) next time it's shown
						orphanServers.erase(packId);
						Toasts::Push(Toasts::Kind::Success, "Server deleted", packName + "'s server files were removed.");
					} else {
						state.busy = false;
						state.deleting = false;
						state.error = error;
						log << "Delete server " << packId << " failed: " << error << "\n";
						Toasts::Push(Toasts::Kind::Error, "Couldn't delete the server", error);
					}
				});
			});
		};
		std::string deleteServerPackId; // server whose "Delete server" confirmation is open

		// Docker (docker-minecraft-server). Checked on a worker each time the Servers page
		// opens: "docker version" fails when Docker is missing or its engine isn't running.
		enum class DockerState { Unknown, Checking, Missing, Stopped, Ready };
		DockerState dockerState = DockerState::Unknown;
		auto checkDocker = [&]() {
			if (dockerState == DockerState::Checking)
				return;
			dockerState = DockerState::Checking;
			StartWorker(workers, [&uiQueue, &dockerState, &shuttingDown]() {
				std::error_code ec;
				const fs::path logFile = fs::absolute(fs::path(kServersRoot) / "docker-check.log", ec);
				fs::create_directories(logFile.parent_path(), ec);
				int exitCode = -1;
				std::string error;
				const bool started = RunProcess({ "docker", "version", "--format", "{{.Server.Version}}" }, fs::current_path(ec),
					logFile, true, &exitCode, &error, &shuttingDown);
				const DockerState state = !started ? DockerState::Missing : exitCode == 0 ? DockerState::Ready : DockerState::Stopped;
				uiQueue.Post([&dockerState, state]() { dockerState = state; });
			});
		};

		// Writes the server's docker-compose.yml, then runs "docker compose <args>" in its folder.
		auto startDockerCompose = [&](const std::string& packId, const std::vector<std::string>& args, const std::string& doneMessage) {
			ServerState& state = servers[packId];
			const fs::path dir = fs::absolute(fs::path(kServersRoot) / packId);
			std::string error;
			if (!WriteDockerCompose(dir, packId, state.info, state.memoryMb, &error)) {
				state.dockerFailed = true;
				state.dockerMessage = error;
				return;
			}
			state.dockerBusy = true;
			state.dockerFailed = false;
			std::vector<std::string> command = { "docker" };
			command.insert(command.end(), args.begin(), args.end());
			state.dockerMessage = "Running";
			for (const std::string& arg : command)
				state.dockerMessage += " " + arg;
			state.dockerMessage += "...";
			log << "Docker for " << packId << ": " << state.dockerMessage << "\n";
			StartWorker(workers, [&uiQueue, &servers, &log, &shuttingDown, packId, dir, command, doneMessage]() {
				int exitCode = -1;
				std::string error;
				const fs::path logFile = dir / "logs" / "docker.log";
				std::error_code ec;
				fs::create_directories(logFile.parent_path(), ec);
				const bool ok = RunProcess(command, dir, logFile, true, &exitCode, &error, &shuttingDown, false) && exitCode == 0;
				if (error.empty() && !ok)
					error = "Docker exited with code " + std::to_string(exitCode) + ". See " + logFile.string();
				uiQueue.Post([&servers, &log, packId, ok, error, doneMessage]() {
					ServerState& state = servers[packId];
					state.dockerBusy = false;
					state.dockerFailed = !ok;
					state.dockerMessage = ok ? doneMessage : error;
					log << "Docker for " << packId << (ok ? ": done" : " failed: " + error) << "\n";
				});
			});
		};

		// Extracted .mrpacks and whole-pack installs can both be hosted (see ServerSetup.h).
		// Checks the active version's install folder.
		auto canHostServer = [&](const Modpack& pack) {
			return CanCreateServer(installDir(pack));
		};

		auto startPlay = [&](const Modpack& pack) {
			if (account.type == Account::Type::None) {
				std::lock_guard<std::mutex> lock(playsMutex);
				plays[pack.id].error = "Sign in with Microsoft or choose an offline name in the sidebar first.";
				return;
			}
			auto cancel = std::make_shared<std::atomic<bool>>(false);
			auto stop = std::make_shared<std::atomic<bool>>(false);
			{
				std::lock_guard<std::mutex> lock(playsMutex);
				PlayState& state = plays[pack.id];
				state = PlayState{};
				state.busy = true;
				state.step = "Starting";
				state.cancel = cancel;
				state.stop = stop;
			}
			PlayRequest request;
			request.gameRoot = fs::absolute(kGameRoot);
			request.extractedDir = fs::absolute(installDir(pack));
			request.instanceDir = fs::absolute(gameDir(pack));
			request.account = account;
			request.launch = LaunchSettingsFor(settings, pack.id);
			request.gameOptions = settings.gameOptions;
			request.stopGame = stop.get(); // kept alive by the worker's copy of stop
			const std::string clientId = settings.microsoftClientId;
			const std::string packId = pack.id;
			const std::string packName = pack.name;
			const AfterLaunch afterLaunch = settings.launch.afterLaunch;
			log << "Play " << packId << " as " << account.name << "\n";
			log.flush();
			StartWorker(workers, [&http, &uiQueue, &plays, &playsMutex, &updateAccount, &log, window, request, clientId, packId,
				packName, afterLaunch, cancel, stop]() mutable {
				bool started = false; // Minecraft's process is running
				auto progress = [&](const std::string& step, double fraction) {
					std::lock_guard<std::mutex> lock(playsMutex);
					PlayState& state = plays[packId];
					state.step = step;
					state.progress = fraction;
					state.playing = step == "Playing";
					if (state.playing && !started) {
						started = true;
						// "When the game starts" from Settings > Minecraft.
						uiQueue.Post([window, afterLaunch, packName]() {
							if (afterLaunch == AfterLaunch::Minimize)
								glfwIconifyWindow(window);
							else if (afterLaunch == AfterLaunch::Close)
								glfwSetWindowShouldClose(window, GLFW_TRUE); // the game keeps running
							else
								Toasts::Push(Toasts::Kind::Success, "Minecraft started", packName);
						});
					}
				};
				std::string error;
				bool ok = true;
				// Microsoft tokens last about a day; refresh one that expires within 5 minutes.
				if (request.account.type == Account::Type::Microsoft &&
					request.account.accessExpires < static_cast<std::int64_t>(std::time(nullptr)) + 300) {
					progress("Refreshing Microsoft sign-in", -1.0);
					ok = RefreshMicrosoftAccount(http, clientId, request.account, &error, cancel.get());
					if (ok) {
						const Account refreshed = request.account;
						uiQueue.Post([&updateAccount, refreshed]() { updateAccount(refreshed); });
					}
				}
				if (ok)
					ok = PlayModpack(http, request, progress, &error, cancel.get());
				{
					std::lock_guard<std::mutex> lock(playsMutex);
					PlayState& state = plays[packId];
					state.busy = false;
					state.playing = false;
					state.error = (ok || cancel->load() || stop->load()) ? std::string() : error;
				}
				const bool cancelled = cancel->load() || stop->load();
				const bool stopped = stop->load();
				uiQueue.Post([&log, window, packId, packName, ok, cancelled, stopped, started, afterLaunch, error]() {
					if (ok)
						log << "Minecraft closed (" << packId << ")\n";
					else
						log << "Play " << packId << " failed: " << error << "\n";
					if (started && afterLaunch == AfterLaunch::Minimize)
						glfwRestoreWindow(window);
					if (stopped)
						Toasts::Push(Toasts::Kind::Info, "Minecraft was closed", packName);
					else if (!ok && !cancelled)
						Toasts::Push(Toasts::Kind::Error, "Couldn't play " + packName, error);
				});
			});
		};

		auto startSearch = [&](SourceView& view) {
			const int generation = ++view.searchGeneration;
			const std::string query = view.search;
			SearchFilters filters;
			filters.gameVersion = view.gameVersion;
			if (view.loader > 0 && view.loader <= static_cast<int>(view.caps.loaders.size()))
				filters.loader = view.caps.loaders[static_cast<std::size_t>(view.loader - 1)];
			filters.sort = view.sort;
			const int limit = settings.searchLimit;
			view.searching = true;
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &view, &localPacks, &log, query, filters, limit, generation]() {
				SourceSearchResult result;
				std::string error;
				const bool ok = SearchPacks(http, view.source, query, filters, limit, result, &error, &shuttingDown);
				uiQueue.Post([&view, &localPacks, &log, query, generation, ok, result, error]() {
					if (generation != view.searchGeneration)
						return;
					const char* sourceName = PackSourceName(view.source);
					view.searching = false;
					view.searchFailed = !ok;
					view.totalHits = ok ? result.totalHits : 0;
					++view.revision;
					if (ok) {
						view.packs = result.packs;
						view.resultsAt = ImGui::GetTime();
						view.searchStatus = FormatCount(result.totalHits) + " modpacks on " + sourceName;
						log << sourceName << " search \"" << query << "\": " << result.packs.size() << " of "
							<< result.totalHits << " result(s)\n";
					} else {
						log << sourceName << " search \"" << query << "\" failed: " << error << "\n";
						view.packs.clear();
						if (view.source == PackSource::Modrinth) {
							// Offline or API error: fall back to the local index.
							const std::string lowerQuery = ToLower(query);
							for (const Modpack& pack : localPacks) {
								if (lowerQuery.empty() || ContainsIgnoreCase(pack.name, lowerQuery) ||
									ContainsIgnoreCase(pack.description, lowerQuery))
									view.packs.push_back(pack);
							}
							view.searchStatus = "Modrinth unavailable (" + error + ") - showing local packs";
						} else {
							view.searchStatus = std::string(sourceName) + " unavailable (" + error + ")";
						}
					}
					view.selected = view.packs.empty() ? -1 : 0;
				});
			});
		};

		auto startFetchFiles = [&](SourceView& view, const Modpack& pack) {
			const std::string packId = pack.id;
			filesLoading.insert(packId);
			filesErrors.erase(packId);
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &view, &filesLoading, &filesErrors, &log, pack, packId]() {
				PackVersionInfo version;
				std::string error;
				const bool ok = FetchPackVersion(http, pack, version, &error, &shuttingDown);
				uiQueue.Post([&view, &filesLoading, &filesErrors, &log, packId, ok, version, error]() {
					filesLoading.erase(packId);
					if (!ok) {
						log << "Version lookup for " << packId << " failed: " << error << "\n";
						filesErrors[packId] = error;
						return;
					}
					log << "Version lookup for " << packId << ": " << version.versionNumber << " ("
						<< (version.files.empty() ? "id " + version.sourceVersionId : version.files.front().name) << ")\n";
					for (Modpack& item : view.packs) {
						if (item.id == packId)
							ApplyPackVersion(item, version);
					}
				});
			});
		};

		auto startFetchBanner = [&](SourceView& view, const std::string& packId) {
			bannersLoading.insert(packId);
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &view, &bannersLoading, &log, packId]() {
				std::string url;
				std::string error;
				const bool ok = FetchModrinthBanner(http, packId, url, &error, &shuttingDown);
				uiQueue.Post([&view, &bannersLoading, &log, packId, ok, url, error]() {
					bannersLoading.erase(packId);
					if (!ok)
						log << "Banner lookup for " << packId << " failed: " << error << "\n";
					for (Modpack& pack : view.packs) {
						if (pack.id != packId)
							continue;
						pack.bannerHiResUrl = url;
						pack.bannerLoaded = true; // also on failure: keep the thumbnail, don't retry
					}
				});
			});
		};

		auto startFetchDescription = [&](const Modpack& pack) {
			descriptionsLoading.insert(pack.id);
			descriptionErrors.erase(pack.id);
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &packDescriptions, &descriptionsLoading, &descriptionErrors, &log, pack]() {
				std::string text, error;
				const bool ok = FetchPackDescription(http, pack, text, &error, &shuttingDown);
				uiQueue.Post([&packDescriptions, &descriptionsLoading, &descriptionErrors, &log, packId = pack.id, ok, text, error]() {
					descriptionsLoading.erase(packId);
					if (ok) {
						packDescriptions[packId] = text;
					} else {
						log << "Description for " << packId << " failed: " << error << "\n";
						descriptionErrors[packId] = error;
					}
				});
			});
		};

		auto startFetchMods = [&](const Modpack& pack) {
			const std::string key = modsKey(pack);
			modsLoading.insert(key);
			modsErrors.erase(key);
			// CurseForge reads the pack zip's manifest; it lands where the install looks for it.
			const std::string workDir = (fs::path("downloads") / pack.id).string();
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &packMods, &modsLoading, &modsErrors, &log, pack, key, workDir]() {
				PackModList list;
				std::string error;
				const bool ok = FetchPackMods(http, pack, workDir, list, &error, &shuttingDown);
				uiQueue.Post([&packMods, &modsLoading, &modsErrors, &log, key, packId = pack.id, ok, list, error]() {
					modsLoading.erase(key);
					if (ok) {
						log << "Mod list for " << packId << ": " << list.mods.size() << " mod(s)\n";
						packMods[key] = list;
					} else {
						log << "Mod list for " << packId << " failed: " << error << "\n";
						modsErrors[key] = error;
					}
				});
			});
		};

		// Installs a whole pack (sources whose capabilities say installsPacks): builds the
		// source's install plan, runs it into the version's own folder (installs/<id>@<version>/,
		// or the existing one when reinstalling), then adds the version to My Library.
		auto startPlanInstall = [&](const Modpack& pack) {
			const std::string key = "pack:" + pack.id + "@" + VersionKey(pack);
			const InstalledVersion version = PlanInstalledVersion(libraryPacks, pack, true);
			{
				std::lock_guard<std::mutex> lock(downloadsMutex);
				DownloadState started;
				started.fileName = pack.version.empty() ? pack.name : pack.name + " " + pack.version;
				started.pack = pack;
				started.active = true;
				started.planInstall = true;
				started.stage = "Preparing";
				downloads[key] = std::move(started);
			}
			log << "Installing " << pack.id << " " << pack.version << " from " << PackSourceName(pack.source) << "\n";
			StartWorker(workers, [&http, &downloads, &downloadsMutex, &shuttingDown, &uiQueue, &libraryPacks, &log,
				&onVersionInstalled, key, pack, version]() {
				auto setStage = [&](const std::string& stage, std::uint64_t done, std::uint64_t total) {
					std::lock_guard<std::mutex> lock(downloadsMutex);
					DownloadState& current = downloads[key];
					current.stage = stage;
					current.stageDone = done;
					current.stageTotal = total;
				};
				std::string error;
				InstallPlan plan;
				std::vector<PlannedFile> blocked;
				const std::string workDir = (fs::path("downloads") / pack.id).string();
				std::error_code dirError;
				fs::create_directories(workDir, dirError);
				bool ok = BuildInstallPlan(http, pack, workDir, plan,
					[&](const std::string& status) { setStage(status, 0, 0); }, &error, &shuttingDown);
				if (ok) {
					ok = RunInstallPlan(http, plan, version.installDir,
						[&](const InstallProgress& progress) { setStage(progress.stage, progress.done, progress.total); },
						blocked, &error, &shuttingDown);
				}
				{
					std::lock_guard<std::mutex> lock(downloadsMutex);
					DownloadState& current = downloads[key];
					current.active = false;
					current.installed = ok;
					current.error = ok ? std::string() : error;
					current.blocked = blocked;
					current.notes = plan.notes;
				}
				const bool cancelled = shuttingDown.load();
				uiQueue.Post([&libraryPacks, &log, &onVersionInstalled, pack, version, ok, cancelled, error,
					blockedCount = blocked.size(), fileCount = plan.files.size()]() {
					if (!ok) {
						log << "Install of " << pack.id << " failed: " << error << "\n";
						if (!cancelled)
							Toasts::Push(Toasts::Kind::Error, "Install failed", pack.name + ": " + error);
						if (onVersionInstalled)
							onVersionInstalled(pack, false);
						return;
					}
					log << "Installed " << pack.id << ": " << fileCount << " file(s), " << blockedCount << " blocked\n";
					AddInstalledVersion(libraryPacks, pack, version);
					if (!SavePackLibrary(kLibraryFile, libraryPacks))
						log << "Could not save " << kLibraryFile << "\n";
					Toasts::Push(Toasts::Kind::Success, "Pack installed", blockedCount > 0
						? pack.name + " " + pack.version + ": " + std::to_string(blockedCount) + " file(s) need a manual download."
						: pack.name + " " + pack.version + " is in your library.");
					if (onVersionInstalled)
						onVersionInstalled(pack, true);
				});
			});
		};

		// Re-checks a library pack's files and re-downloads damaged or missing ones. Whole-pack
		// installs re-run their install plan, which keeps files whose hash still matches.
		auto startRepair = [&](const Modpack& pack) {
			if (GetSourceCapabilities(pack.source).installsPacks) {
				startPlanInstall(pack);
				Toasts::Push(Toasts::Kind::Info, "Repairing " + pack.name, "Progress is on the Downloads page.");
				return;
			}
			auto cancel = std::make_shared<std::atomic<bool>>(false);
			{
				std::lock_guard<std::mutex> lock(playsMutex);
				PlayState& state = plays[pack.id];
				state = PlayState{};
				state.busy = true;
				state.step = "Repairing";
				state.cancel = cancel;
			}
			PlayRequest request;
			request.gameRoot = fs::absolute(kGameRoot);
			request.extractedDir = fs::absolute(installDir(pack));
			request.instanceDir = fs::absolute(gameDir(pack));
			const std::string packId = pack.id;
			const std::string packName = pack.name;
			log << "Repair " << packId << " " << pack.version << "\n";
			StartWorker(workers, [&http, &uiQueue, &plays, &playsMutex, &log, request, packId, packName, cancel]() {
				auto progress = [&](const std::string& step, double fraction) {
					std::lock_guard<std::mutex> lock(playsMutex);
					PlayState& state = plays[packId];
					state.step = step;
					state.progress = fraction;
				};
				std::string error;
				const bool ok = RepairModpack(http, request, progress, &error, cancel.get());
				{
					std::lock_guard<std::mutex> lock(playsMutex);
					PlayState& state = plays[packId];
					state.busy = false;
					state.error = (ok || cancel->load()) ? std::string() : error;
				}
				const bool cancelled = cancel->load();
				uiQueue.Post([&log, packId, packName, ok, cancelled, error]() {
					log << "Repair " << packId << (ok ? " done" : " failed: " + error) << "\n";
					if (ok)
						Toasts::Push(Toasts::Kind::Success, "Repaired " + packName, "Every pack file matches its checksum.");
					else if (!cancelled)
						Toasts::Push(Toasts::Kind::Error, "Repair failed", packName + ": " + error);
				});
			});
		};

		// Uninstalls one installed version of a library pack (versionKey), or with an empty key
		// every version and the pack itself. Deletes the versions' install and game folders
		// (worlds included), plus downloads/<id>/ and the pack's saved downloads when the whole
		// pack goes. Its server folder, if any, is kept. Deleting runs on a worker so big packs
		// don't stall the UI.
		auto uninstallPack = [&](const Modpack& pack, const std::string& versionKey) {
			// The id names folders that get deleted, so it must be one plain path segment.
			if (!IsPlainFolderName(pack.id)) {
				log << "Refusing to uninstall pack with id \"" << pack.id << "\"\n";
				Toasts::Push(Toasts::Kind::Error, "Couldn't uninstall " + pack.name, "Its id isn't a valid folder name.");
				return;
			}
			const auto entry = libraryPacks.find(pack.id);
			if (entry == libraryPacks.end())
				return;
			// Folders come from library.json: only delete ones inside installs/ or instances/
			// that belong to this pack.
			auto ownedFolder = [&](const std::string& dir) {
				const fs::path path = fs::u8path(dir).lexically_normal();
				if (path.empty() || path.is_absolute() || path.has_root_name())
					return false;
				auto part = path.begin();
				const std::string top = part->string();
				if (top != "installs" && top != kInstancesRoot)
					return false;
				if (++part == path.end())
					return false;
				const std::string name = part->string();
				return name == pack.id || name.rfind(pack.id + "@", 0) == 0;
			};
			std::vector<fs::path> folders;
			const bool wholePack = versionKey.empty();
			for (const InstalledVersion& v : entry->second.installedVersions) {
				if (!wholePack && v.key != versionKey)
					continue;
				for (const std::string& dir : { v.installDir, v.gameDir }) {
					if (ownedFolder(dir))
						folders.push_back(fs::u8path(dir));
					else
						log << "Not deleting " << dir << ": outside this pack's folders\n";
				}
			}
			if (wholePack)
				folders.push_back(fs::path("downloads") / pack.id);

			std::string label = pack.name;
			if (wholePack) {
				libraryPacks.erase(entry);
			} else {
				if (const InstalledVersion* v = FindInstalledVersion(entry->second, versionKey))
					label += " " + v->version;
				if (!RemoveInstalledVersion(entry->second, versionKey))
					libraryPacks.erase(entry);
			}
			if (!SavePackLibrary(kLibraryFile, libraryPacks))
				log << "Could not save " << kLibraryFile << "\n";
			{
				std::lock_guard<std::mutex> lock(downloadsMutex);
				for (auto it = downloads.begin(); it != downloads.end();) {
					const bool match = it->second.pack.id == pack.id && !it->second.active &&
						(wholePack || VersionKey(it->second.pack) == versionKey);
					if (match)
						it = downloads.erase(it);
					else
						++it;
				}
			}
			saveDownloads();
			if (!libraryPacks.count(pack.id)) {
				std::lock_guard<std::mutex> lock(playsMutex);
				plays.erase(pack.id);
			}
			log << "Uninstall " << pack.id << (wholePack ? std::string(" (all versions)") : " " + versionKey) << "\n";
			StartWorker(workers, [&uiQueue, &log, folders, packId = pack.id, label]() {
				std::string failed;
				for (const fs::path& dir : folders) {
					std::error_code ec;
					fs::path target = fs::absolute(dir, ec);
#ifdef _WIN32
					// Always the \\?\ form: files deep inside can pass 260 characters even when dir doesn't.
					target = fs::path(L"\\\\?\\" + target.lexically_normal().wstring());
#endif
					fs::remove_all(target, ec);
					if (ec && failed.empty())
						failed = dir.string() + ": " + ec.message();
				}
				uiQueue.Post([&log, packId, label, failed]() {
					if (failed.empty()) {
						Toasts::Push(Toasts::Kind::Success, "Uninstalled " + label, "Its files were deleted.");
					} else {
						log << "Uninstall " << packId << ": couldn't delete " << failed << "\n";
						Toasts::Push(Toasts::Kind::Error, "Some files couldn't be deleted",
							failed + ". Close anything using them and delete the folder by hand.");
					}
				});
			});
		};
		std::string uninstallPackId; // pack whose "Uninstall" confirmation is open
		bool uninstallDeleteServer = false; // its "Also delete its server" checkbox
		std::string uninstallVersionKey; // the version to uninstall; empty = the whole pack
		std::string launchSettingsPackId; // pack whose "Launch settings" dialog is open

		// ---- Pack updates (UI thread only) ----
		// An update installs the source's newest version side by side, carries the player's
		// worlds and options over from the version it replaces, then (optionally) uninstalls
		// the old version.
		struct PackUpdate {
			std::string fromKey;   // the version being updated from
			std::string toKey;     // VersionKey of the version being installed
			std::string toVersion; // its display name
			bool removeOld = false;
			bool copying = false;  // the install finished; player data is being copied
		};
		std::map<std::string, PackUpdate> packUpdates; // running updates, keyed by pack id
		// The "Update" dialog: the newest version is looked up when it opens.
		struct UpdateCheck {
			std::string packId; // empty = closed
			bool loading = false;
			std::string error;
			Modpack target;     // the pack at its newest version, once loaded
			bool removeOld = false;
		};
		UpdateCheck updateCheck;
		int updateCheckGeneration = 0;
		auto startUpdateCheck = [&](const Modpack& pack) {
			updateCheck = UpdateCheck{};
			updateCheck.packId = pack.id;
			updateCheck.loading = true;
			const int generation = ++updateCheckGeneration;
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &updateCheck, &updateCheckGeneration, &log, pack, generation]() {
				PackVersionInfo newest;
				std::string error;
				const bool ok = FetchPackVersion(http, pack, newest, &error, &shuttingDown);
				uiQueue.Post([&updateCheck, &updateCheckGeneration, &log, pack, generation, ok, newest, error]() {
					if (generation != updateCheckGeneration || updateCheck.packId != pack.id)
						return; // the dialog was closed or opened for another pack
					updateCheck.loading = false;
					if (!ok) {
						updateCheck.error = error;
						log << "Update check for " << pack.id << " failed: " << error << "\n";
						return;
					}
					// The library pack, moved to the newest version. ATLauncher keeps its optional mod choices.
					Modpack target = pack;
					target.installedVersions.clear();
					target.activeVersion.clear();
					ApplyPackVersion(target, newest);
					updateCheck.target = target;
					log << "Newest version of " << pack.id << ": " << target.version << "\n";
				});
			});
		};
		// The file an update downloads for per-file sources (Modrinth): the .mrpack.
		auto updateFile = [](const Modpack& target) -> const PackFile* {
			for (const PackFile& file : target.files) {
				if (file.name.size() > 7 && file.name.compare(file.name.size() - 7, 7, ".mrpack") == 0)
					return &file;
			}
			return target.files.empty() ? nullptr : &target.files.front();
		};
		auto startUpdate = [&](const Modpack& libraryPack, const Modpack& target, bool removeOld) {
			const bool wholePack = GetSourceCapabilities(target.source).installsPacks;
			const PackFile* file = wholePack ? nullptr : updateFile(target);
			if (!wholePack && !file) {
				Toasts::Push(Toasts::Kind::Error, "Couldn't update " + libraryPack.name, "The new version has no file to install.");
				return;
			}
			packUpdates[libraryPack.id] = { libraryPack.activeVersion, VersionKey(target), target.version, removeOld, false };
			log << "Update " << libraryPack.id << " from " << libraryPack.activeVersion << " to " << VersionKey(target) << "\n";
			if (wholePack)
				startPlanInstall(target);
			else
				startDownloadInstall(target, *file);
			Toasts::Push(Toasts::Kind::Info, "Updating " + libraryPack.name + " to " + target.version,
				"Progress is on the Downloads page.");
		};
		// When an update's install finishes: copy the player's data into the new version's game
		// folder on a worker, then remove the old version if asked.
		onVersionInstalled = [&](const Modpack& pack, bool ok) {
			const auto running = packUpdates.find(pack.id);
			if (running == packUpdates.end() || running->second.copying || running->second.toKey != VersionKey(pack))
				return;
			if (!ok) {
				packUpdates.erase(running); // the install already reported its error
				return;
			}
			const auto entry = libraryPacks.find(pack.id);
			const InstalledVersion* from = entry == libraryPacks.end() ? nullptr : FindInstalledVersion(entry->second, running->second.fromKey);
			const InstalledVersion* to = entry == libraryPacks.end() ? nullptr : FindInstalledVersion(entry->second, running->second.toKey);
			if (!from || !to || from->key == to->key) {
				packUpdates.erase(running);
				return;
			}
			running->second.copying = true;
			const fs::path fromDir = fs::absolute(fs::u8path(from->gameDir));
			const fs::path toDir = fs::absolute(fs::u8path(to->gameDir));
			StartWorker(workers, [&uiQueue, &shuttingDown, &packUpdates, &libraryPacks, &plays, &playsMutex, &uninstallPack, &log,
				packId = pack.id, fromDir, toDir]() {
				std::size_t copied = 0;
				std::string error;
				const bool copiedOk = CarryOverPlayerData(fromDir, toDir, copied, &error, &shuttingDown);
				uiQueue.Post([&packUpdates, &libraryPacks, &plays, &playsMutex, &uninstallPack, &log, packId, copiedOk, copied, error]() {
					const auto running = packUpdates.find(packId);
					if (running == packUpdates.end())
						return;
					const PackUpdate update = running->second;
					packUpdates.erase(running);
					const auto entry = libraryPacks.find(packId);
					if (entry == libraryPacks.end())
						return;
					const std::string name = entry->second.name;
					log << "Update " << packId << ": copied " << copied << " file(s) of player data"
						<< (copiedOk ? std::string() : ", then failed: " + error) << "\n";
					if (!copiedOk) {
						Toasts::Push(Toasts::Kind::Error, "Couldn't copy your worlds and settings",
							name + " " + update.toVersion + " is installed, but " + error + ". The old version was kept.");
						return;
					}
					bool playing = false;
					{
						std::lock_guard<std::mutex> lock(playsMutex);
						const auto it = plays.find(packId);
						playing = it != plays.end() && (it->second.busy || it->second.playing);
					}
					if (update.removeOld && !playing && FindInstalledVersion(entry->second, update.fromKey)) {
						uninstallPack(entry->second, update.fromKey);
						Toasts::Push(Toasts::Kind::Success, "Updated " + name + " to " + update.toVersion,
							"Your worlds and settings came along; the old version is being removed.");
					} else {
						Toasts::Push(Toasts::Kind::Success, "Updated " + name + " to " + update.toVersion,
							update.removeOld ? "Your worlds and settings came along. The old version was kept because the game was running."
							: "Your worlds and settings came along. The old version is still installed.");
					}
				});
			});
		};

		// The optional-mods picker shown before installing a pack that has some (ATLauncher).
		struct OptionalPicker {
			Modpack pack;               // empty id = closed
			bool loading = false;
			std::string error;
			std::vector<OptionalMod> mods;
			std::vector<char> chosen;   // parallel to mods
			int generation = 0;
		};
		OptionalPicker picker;

		// Every version of a pack, for the version dropdowns (UI thread only, keyed by pack id).
		struct VersionList {
			bool loading = false;
			bool loaded = false;
			std::string error;
			std::vector<PackVersionInfo> versions;
		};
		std::map<std::string, VersionList> versionLists;
		auto startListVersions = [&](const Modpack& pack) {
			VersionList& list = versionLists[pack.id];
			list.loading = true;
			list.error.clear();
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &versionLists, &log, pack]() {
				std::vector<PackVersionInfo> versions;
				std::string error;
				const bool ok = ListPackVersions(http, pack, versions, &error, &shuttingDown);
				uiQueue.Post([&versionLists, &log, id = pack.id, ok, versions, error]() {
					VersionList& list = versionLists[id];
					list.loading = false;
					list.loaded = ok;
					list.error = ok ? std::string() : error;
					list.versions = versions;
					log << "Versions of " << id << ": " << (ok ? std::to_string(versions.size()) : "failed: " + error) << "\n";
				});
			});
		};
		// The details after a version in a dropdown row, e.g. "Minecraft 1.20.1 - beta - 2024-05-01".
		auto versionDetails = [](const PackVersionInfo& v) {
			std::string details;
			auto add = [&](const std::string& part) {
				if (!part.empty())
					details += (details.empty() ? "" : "  \xC2\xB7  ") + part;
			};
			add(v.gameVersion.empty() ? std::string() : "Minecraft " + v.gameVersion);
			add(v.versionType == "release" ? std::string() : v.versionType);
			add(v.published);
			return details;
		};
		// A dropdown of the pack's versions; the list loads the first time it opens. currentId
		// (a sourceVersionId) is highlighted. Returns true when the player picked one.
		auto versionCombo = [&](const char* id, const Modpack& pack, const std::string& preview, const std::string& currentId,
			PackVersionInfo& picked) {
			bool chosen = false;
			if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
				VersionList& list = versionLists[pack.id];
				if (!list.loaded && !list.loading && list.error.empty())
					startListVersions(pack);
				if (list.loading) {
					Widgets::Spinner(Theme::C().accent);
					ImGui::SameLine();
					ImGui::TextDisabled("Loading versions...");
				} else if (!list.error.empty()) {
					ImGui::TextColored(Theme::C().error, "Couldn't load versions: %s", list.error.c_str());
					if (ImGui::Selectable("Retry", false, ImGuiSelectableFlags_DontClosePopups))
						startListVersions(pack);
				}
				for (std::size_t i = 0; i < list.versions.size(); ++i) {
					const PackVersionInfo& v = list.versions[i];
					ImGui::PushID(static_cast<int>(i));
					const bool current = !currentId.empty() && v.sourceVersionId == currentId;
					if (ImGui::Selectable(v.versionNumber.empty() ? v.sourceVersionId.c_str() : v.versionNumber.c_str(), current)) {
						picked = v;
						chosen = true;
					}
					if (current)
						ImGui::SetItemDefaultFocus();
					const std::string details = (i == 0 ? std::string("newest  \xC2\xB7  ") : std::string()) + versionDetails(v);
					if (!details.empty()) {
						ImGui::SameLine();
						ImGui::TextDisabled("%s", details.c_str());
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			return chosen;
		};
		// Puts a picked version on a pack. Its Minecraft version replaces the old one even
		// when unknown, so the pack doesn't show the previous version's.
		auto applyPickedVersion = [](Modpack& target, const PackVersionInfo& v) {
			target.gameVersion = v.gameVersion;
			ApplyPackVersion(target, v);
		};
		// True when v is the version a library pack has installed. Modrinth packs installed
		// before versions had ids only know their version number.
		auto isInstalledVersion = [](const Modpack& pack, const PackVersionInfo& v) {
			return pack.sourceVersionId.empty() ? v.versionNumber == pack.version : v.sourceVersionId == pack.sourceVersionId;
		};

		// Checks or unchecks an optional mod, keeping the choice consistent: checking adds the
		// mods it depends on and unchecks others in its group; unchecking drops mods that need it.
		std::function<void(std::size_t, bool)> setOptional = [&](std::size_t index, bool on) {
			if (index >= picker.mods.size() || (picker.chosen[index] != 0) == on)
				return;
			picker.chosen[index] = on ? 1 : 0;
			const OptionalMod& mod = picker.mods[index];
			for (std::size_t i = 0; i < picker.mods.size(); ++i) {
				const OptionalMod& other = picker.mods[i];
				if (on && i != index && !mod.group.empty() && other.group == mod.group)
					setOptional(i, false);
				if (on && std::find(mod.depends.begin(), mod.depends.end(), other.name) != mod.depends.end())
					setOptional(i, true);
				if (!on && std::find(other.depends.begin(), other.depends.end(), mod.name) != other.depends.end())
					setOptional(i, false);
			}
		};

		// Install pack: ATLauncher packs first offer their optional mods; everything else installs right away.
		auto requestPlanInstall = [&](const Modpack& pack) {
			if (pack.source != PackSource::ATLauncher) {
				startPlanInstall(pack);
				return;
			}
			picker = OptionalPicker{ pack, true, {}, {}, {}, picker.generation + 1 };
			// A reinstall of this version starts from the choices made for it last time.
			const auto saved = libraryPacks.find(pack.id);
			const InstalledVersion* savedVersion = saved != libraryPacks.end()
				? FindInstalledVersion(saved->second, VersionKey(pack)) : nullptr;
			const std::vector<std::string> previous = savedVersion ? savedVersion->optionalMods : std::vector<std::string>();
			const bool hadChoice = savedVersion != nullptr;
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &picker, &setOptional, &startPlanInstall, pack, previous,
				hadChoice, generation = picker.generation]() {
				std::vector<OptionalMod> mods;
				std::string error;
				const bool ok = FetchOptionalMods(http, pack, mods, &error, &shuttingDown);
				uiQueue.Post([&picker, &setOptional, &startPlanInstall, pack, previous, hadChoice, generation, ok, mods, error]() {
					if (picker.generation != generation || picker.pack.id.empty())
						return; // cancelled or replaced
					picker.loading = false;
					if (!ok) {
						picker.error = error;
						return;
					}
					if (mods.empty()) {
						picker.pack = Modpack{};
						startPlanInstall(pack);
						return;
					}
					picker.mods = mods;
					picker.chosen.assign(mods.size(), 0);
					for (std::size_t i = 0; i < mods.size(); ++i) {
						const bool wanted = hadChoice
							? std::find(previous.begin(), previous.end(), mods[i].name) != previous.end()
							: mods[i].selected;
						if (wanted)
							setOptional(i, true);
					}
				});
			});
		};

		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
			ReapFinishedWorkers(workers);
			uiQueue.Drain();

			// Rebuild the style when the window moves to a monitor with a different DPI,
			// or the theme or UI scale changed last frame.
			float contentScale = dpiScale;
			glfwGetWindowContentScale(window, &contentScale, nullptr);
			Theme::SetCustomThemes(settings.customThemes); // picks up edits from the theme editor
			const std::size_t wantedTheme = Theme::FindTheme(settings.theme);
			const bool themeChanged = wantedTheme != appliedTheme || Theme::Revision() != appliedThemeRevision;
			if (contentScale > 0.0f && (contentScale != dpiScale || settings.uiScale != appliedUiScale || themeChanged)) {
				appliedThemeRevision = Theme::Revision();
				dpiScale = contentScale;
				appliedUiScale = settings.uiScale;
				appliedTheme = wantedTheme;
				Theme::Apply(appliedTheme, dpiScale, appliedUiScale);
				if (themeChanged)
					ApplyTitleBarColors(window, Theme::C());
				glfwSetWindowSizeLimits(window, static_cast<int>(S(900.0f)), static_cast<int>(S(600.0f)), GLFW_DONT_CARE, GLFW_DONT_CARE);
				settingsContext.dpiScale = dpiScale;
			}
			Widgets::SetReduceMotion(settings.reduceMotion);
			if (!glfwGetWindowAttrib(window, GLFW_ICONIFIED))
				lastPlacement.maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
			if (!glfwGetWindowAttrib(window, GLFW_MAXIMIZED) && !glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
				glfwGetWindowPos(window, &lastPlacement.x, &lastPlacement.y);
				glfwGetWindowSize(window, &lastPlacement.width, &lastPlacement.height);
			}
			mascot.SetReduceMotion(settings.reduceMotion);
			// Save after edits, but not on every keystroke or slider step.
			if (settingsDirty && !ImGui::IsAnyItemActive()) {
				settingsDirty = false;
				if (!SaveSettings(kSettingsFile, settings))
					log << "Could not save " << kSettingsFile << "\n";
			}
			images.Update(ImGui::GetTime());

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			// Ctrl + / Ctrl - / Ctrl 0 change the UI scale (applied next frame). Ctrl F jumps to search.
			auto setUiScale = [&](float scale) {
				settings.uiScale = NormalizeUiScale(scale);
				settingsDirty = true;
			};
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Equal) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd))
				setUiScale(settings.uiScale + kUiScaleStep);
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Minus) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract))
				setUiScale(settings.uiScale - kUiScaleStep);
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_0) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Keypad0))
				setUiScale(1.0f);
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
				page = Page::Discover;
				focusSearch = true;
			}

			const Theme::Fonts& fonts = Theme::GetFonts();

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::Begin("MC_Launcher_Premium", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

			// ---- Sidebar ----
			ImGui::BeginChild("sidebar", ImVec2(S(230.0f), 0.0f), ImGuiChildFlags_Borders,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			{
				// Transparent interactive GLB fox mascot, then the launcher name.
				if (mascot.Draw(S(56.0f), mascotTooltip.c_str()))
					rollMascot();
				ImGui::SameLine(0.0f, S(8.0f));
				ImGui::BeginGroup();
				Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::C().text, "MC Launcher");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(6.0f));
				Widgets::Text(fonts.bold, Theme::kFontSmall, Theme::C().accent, "PREMIUM  \xC2\xB7  JAVA");
				ImGui::EndGroup();
			}
			ImGui::Dummy(ImVec2(0.0f, S(14.0f)));
			Widgets::SectionLabel("BROWSE");
			if (Widgets::NavItem("Discover", page == Page::Discover))
				page = Page::Discover;
			if (Widgets::NavItem("My Library", page == Page::Library))
				page = Page::Library;
			int activeDownloads = 0;
			{
				std::lock_guard<std::mutex> lock(downloadsMutex);
				for (const auto& [url, state] : downloads) {
					if (state.active || state.installing)
						++activeDownloads;
				}
			}
			if (Widgets::NavItem("Downloads", page == Page::Downloads, activeDownloads))
				page = Page::Downloads;
			int runningServers = 0;
			for (const auto& [packId, server] : servers) {
				if (server.running)
					++runningServers;
			}
			if (Widgets::NavItem("Servers", page == Page::Servers, runningServers))
				page = Page::Servers;
			ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
			Widgets::SectionLabel("LAUNCHER");
			if (Widgets::NavItem("Settings", page == Page::Settings))
				page = Page::Settings;

			// Account: who Minecraft is launched as.
			ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
			Widgets::SectionLabel("ACCOUNT");
			auto accountKind = [](const Account& a) {
				return a.type == Account::Type::Microsoft ? "Microsoft account" : "Offline (singleplayer)";
			};
			const bool hasClientId = !settings.microsoftClientId.empty();
			if (account.type != Account::Type::None && signIn != SignIn::Starting) {
				if (accounts.accounts.size() > 1) {
					// Switch which saved account Play uses.
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::BeginCombo("##account", account.name.c_str())) {
						for (std::size_t i = 0; i < accounts.accounts.size(); ++i) {
							const Account& item = accounts.accounts[i];
							ImGui::PushID(static_cast<int>(i));
							if (ImGui::Selectable(item.name.c_str(), i == accounts.active)) {
								accounts.active = i;
								saveAccounts();
							}
							ImGui::SameLine();
							ImGui::TextDisabled("%s", item.type == Account::Type::Microsoft ? "Microsoft" : "Offline");
							ImGui::PopID();
						}
						ImGui::EndCombo();
					}
				} else {
					ImGui::PushFont(fonts.bold, 0.0f);
					ImGui::TextUnformatted(account.name.c_str());
					ImGui::PopFont();
				}
				ImGui::TextDisabled("%s", accountKind(account));
				const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
				if (ImGui::Button("Add account", ImVec2(half, 0.0f)))
					ImGui::OpenPopup("add_account");
				ImGui::SameLine();
				if (ImGui::Button("Sign out", ImVec2(-1.0f, 0.0f))) {
					accounts.Remove(accounts.active);
					saveAccounts();
				}
				ImGui::SetItemTooltip("Remove %s from this launcher", account.name.c_str());
				if (ImGui::BeginPopup("add_account")) {
					if (ImGui::MenuItem("Sign in with Microsoft", nullptr, false, hasClientId))
						startMicrosoftSignIn();
					if (ImGui::MenuItem("Play offline..."))
						openOfflinePopup = true;
					ImGui::EndPopup();
				}
				if (!signInError.empty()) {
					ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().error);
					ImGui::TextWrapped("%s", signInError.c_str());
					ImGui::PopStyleColor();
				}
			} else if (signIn == SignIn::Starting) {
				Widgets::Spinner(Theme::C().accent);
				ImGui::SameLine();
				ImGui::TextDisabled(signInCancel->load()
					? "Cancelling sign-in..." : "Waiting for sign-in in your browser...");
				ImGui::BeginDisabled(signInCancel->load());
				if (ImGui::Button("Cancel", ImVec2(-1.0f, 0.0f)))
					signInCancel->store(true);
				ImGui::EndDisabled();
			} else {
				ImGui::BeginDisabled(!hasClientId);
				if (ImGui::Button("Sign in with Microsoft", ImVec2(-1.0f, 0.0f)))
					startMicrosoftSignIn();
				ImGui::EndDisabled();
				if (!hasClientId)
					ImGui::SetItemTooltip("Needs an Azure client ID approved for Minecraft. Set it in Settings > Account.");
				if (ImGui::Button("Play offline...", ImVec2(-1.0f, 0.0f)))
					openOfflinePopup = true;
				ImGui::SetItemTooltip("Singleplayer and offline-mode servers only. For players who own Minecraft.");
				if (!signInError.empty()) {
					ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().error);
					ImGui::TextWrapped("%s", signInError.c_str());
					ImGui::PopStyleColor();
				}
			}

			ImGui::EndChild();

			// Offline name prompt.
			if (openOfflinePopup) {
				ImGui::OpenPopup("Play offline");
				openOfflinePopup = false;
			}
			ImGui::SetNextWindowSize(ImVec2(S(420.0f), 0.0f));
			if (ImGui::BeginPopupModal("Play offline", nullptr, ImGuiWindowFlags_NoResize)) {
				ImGui::TextWrapped("Offline mode skips Microsoft sign-in. You can play singleplayer and join "
					"servers that don't check accounts. Use it only if you own Minecraft: Java Edition.");
				ImGui::Spacing();
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();
				const bool entered = ImGui::InputTextWithHint("##offline_name", "Username", offlineName, sizeof(offlineName),
					ImGuiInputTextFlags_EnterReturnsTrue);
				const bool valid = IsValidPlayerName(offlineName);
				if (!valid && offlineName[0] != '\0')
					ImGui::TextColored(Theme::C().error, "3-16 letters, numbers or underscores.");
				ImGui::BeginDisabled(!valid);
				if (ImGui::Button("Use this name") || (entered && valid)) {
					addAccount(MakeOfflineAccount(offlineName));
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button("Cancel##offline"))
					ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}

			// ---- Main content ----
			ImGui::SameLine();
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24.0f), S(18.0f)));
			ImGui::BeginChild("main_content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
				ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::PopStyleVar();
			const int pageKey = static_cast<int>(page);
			if (pageKey != shownPageKey) {
				shownPageKey = pageKey;
				pageShownAt = ImGui::GetTime();
				if (page == Page::Servers) {
					refreshOrphanServers();
					lanAddress = Platform::LocalNetworkAddress();
					hostAddresses = Platform::LocalNetworkAddresses();
					checkDocker();
				}
			}
			Widgets::BeginEntrance(pageShownAt);
			const char* breadcrumb = page == Page::Discover ? "MINECRAFT  /  MODPACKS"
				: page == Page::Library ? "MINECRAFT  /  LIBRARY"
				: page == Page::Downloads ? "MINECRAFT  /  DOWNLOADS"
				: page == Page::Servers ? "MINECRAFT  /  SERVERS" : "LAUNCHER  /  SETTINGS";
			Widgets::Text(fonts.bold, Theme::kFontSmall, Theme::C().accent, breadcrumb);
			if (page == Page::Discover) {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::C().text, "Discover modpacks");
				ImGui::TextDisabled("Search Modrinth, CurseForge, FTB, Technic and ATLauncher at once.");
			} else if (page == Page::Library) {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::C().text, "My library");
				ImGui::TextDisabled("Modpack files you have extracted are listed here.");
			} else if (page == Page::Downloads) {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::C().text, "Downloads");
				ImGui::TextDisabled("Track downloads and extraction progress.");
			} else if (page == Page::Servers) {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::C().text, "Servers");
				ImGui::TextDisabled("Host a modpack for your friends in one click.");
			} else {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::C().text, "Settings");
				ImGui::TextDisabled("Themes, Minecraft launch defaults, and more.");
			}
			ImGui::Spacing();

			if (page == Page::Discover) {
				// Which sources the search covers: the source filter (empty = all), minus sources
				// that can't be used and, when a loader is chosen, sources that can't filter by it.
				std::vector<PackSource> active;
				std::vector<std::string> skipped;
				for (PackSource source : OnlineSources()) {
					if (!discover.sources.empty() && discover.sources.count(source) == 0)
						continue;
					std::string reason;
					if (!IsSourceAvailable(source, &reason)) {
						skipped.push_back(std::string(PackSourceName(source)) + ": " + reason);
						continue;
					}
					if (!discover.loader.empty() && !views[source].caps.loaderFilter) {
						skipped.push_back(std::string(PackSourceName(source)) + " can't filter by loader");
						continue;
					}
					active.push_back(source);
				}
				const bool single = active.size() == 1;
				const SourceCapabilities* singleCaps = single ? &views[active.front()].caps : nullptr;
				const int sortCount = single ? static_cast<int>(singleCaps->sorts.size()) : 3;
				if (discover.sort >= sortCount)
					discover.sort = 0;

				// Search box.
				ImGui::SetNextItemWidth(-1.0f);
				if (focusSearch) {
					ImGui::SetKeyboardFocusHere();
					focusSearch = false;
				}
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14.0f), S(11.0f)));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
				const char* hint = single ? singleCaps->searchHint : "Search modpacks on every source...";
				if (ImGui::InputTextWithHint("##search", hint, discover.search, sizeof(discover.search))) {
					discover.searchPending = true;
					discover.lastSearchEdit = ImGui::GetTime();
				}
				ImGui::PopStyleVar(2);

				// Source filter chips.
				bool filtersChanged = false; // chips and combos search right away; text waits for typing to pause
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12.0f), S(5.0f))); // the chips' padding
				ImGui::AlignTextToFramePadding();
				ImGui::PopStyleVar();
				ImGui::TextDisabled("Sources");
				ImGui::SameLine();
				if (Widgets::ToggleChip("All", discover.sources.empty(), Theme::C().accent) && !discover.sources.empty()) {
					discover.sources.clear();
					filtersChanged = true;
				}
				for (PackSource source : OnlineSources()) {
					std::string reason;
					const bool available = IsSourceAvailable(source, &reason);
					const bool on = discover.sources.empty() || discover.sources.count(source) > 0;
					ImGui::SameLine();
					if (Widgets::ToggleChip(PackSourceName(source), on && available, SourceColor(source), available)) {
						if (discover.sources.empty())
							discover.sources = { source }; // from "all", a click picks just this one
						else if (discover.sources.count(source))
							discover.sources.erase(source);
						else
							discover.sources.insert(source);
						if (discover.sources.size() == OnlineSources().size())
							discover.sources.clear();
						filtersChanged = true;
					}
					if (!available)
						ImGui::SetItemTooltip("%s", reason.c_str());
					else if (discover.sources.empty())
						ImGui::SetItemTooltip("Show only %s", PackSourceName(source));
				}

				// Minecraft version, loader, sort.
				ImGui::SetNextItemWidth(S(170.0f));
				if (ImGui::InputTextWithHint("##mcversion", "Minecraft version", discover.gameVersion, sizeof(discover.gameVersion))) {
					discover.searchPending = true;
					discover.lastSearchEdit = ImGui::GetTime();
				}
				ImGui::SetItemTooltip("Only packs for this Minecraft version, e.g. 1.20.1");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(S(150.0f));
				if (ImGui::BeginCombo("##loader", discover.loader.empty() ? "Any loader" : LoaderLabel(discover.loader.c_str()))) {
					if (Widgets::ListItem("Any loader", discover.loader.empty())) {
						discover.loader.clear();
						filtersChanged = true;
					}
					for (const char* key : kLoaderKeys) {
						if (Widgets::ListItem(LoaderLabel(key), discover.loader == key)) {
							discover.loader = key;
							filtersChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(S(210.0f));
				const char* sortLabel = single ? singleCaps->sorts[static_cast<std::size_t>(discover.sort)] : kCombinedSorts[discover.sort];
				if (ImGui::BeginCombo("##sort", (std::string("Sort: ") + sortLabel).c_str())) {
					for (int i = 0; i < sortCount; ++i) {
						if (Widgets::ListItem(single ? singleCaps->sorts[static_cast<std::size_t>(i)] : kCombinedSorts[i], discover.sort == i)) {
							discover.sort = i;
							filtersChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				if (!single)
					ImGui::SetItemTooltip("Best match mixes each source's top results. Pick one source for its own sort options.");
				if (filtersChanged) {
					discover.searchPending = true;
					discover.lastSearchEdit = -1.0; // no typing delay
					settings.discoverSources.clear();
					for (PackSource source : discover.sources)
						settings.discoverSources.push_back(PackSourceKey(source));
					settingsDirty = true;
				}

				// Send the search to every active source.
				if (discover.searchPending && ImGui::GetTime() - discover.lastSearchEdit >= kSearchDelaySeconds) {
					discover.searchPending = false;
					discover.activeSources = active;
					discover.skippedSources = skipped;
					for (PackSource source : active) {
						SourceView& view = views[source];
						std::snprintf(view.search, sizeof(view.search), "%s", discover.search);
						std::snprintf(view.gameVersion, sizeof(view.gameVersion), "%s", discover.gameVersion);
						view.loader = 0;
						for (std::size_t i = 0; i < view.caps.loaders.size(); ++i) {
							if (discover.loader == view.caps.loaders[i])
								view.loader = static_cast<int>(i + 1);
						}
						view.sort = single ? discover.sort : 0;
						startSearch(view);
					}
					discover.mergedKey.clear();
				}

				// Merge the sources' results whenever one of them changes.
				const bool singleResult = discover.activeSources.size() == 1;
				std::string mergeKey = std::to_string(discover.sort);
				for (PackSource source : discover.activeSources)
					mergeKey += "," + std::to_string(views[source].revision);
				if (mergeKey != discover.mergedKey) {
					discover.mergedKey = mergeKey;
					discover.merged.clear();
					std::size_t longest = 0;
					for (PackSource source : discover.activeSources)
						longest = std::max(longest, views[source].packs.size());
					for (std::size_t rank = 0; rank < longest; ++rank) {
						for (PackSource source : discover.activeSources) {
							if (rank < views[source].packs.size())
								discover.merged.emplace_back(source, rank);
						}
					}
					if (!singleResult && discover.sort != 0) {
						auto packOf = [&](const std::pair<PackSource, std::size_t>& ref) -> const Modpack& {
							return views[ref.first].packs[ref.second];
						};
						std::stable_sort(discover.merged.begin(), discover.merged.end(), [&](const auto& a, const auto& b) {
							if (discover.sort == 1)
								return packOf(a).downloads > packOf(b).downloads;
							return ToLower(packOf(a).name) < ToLower(packOf(b).name);
						});
					}
				}

				// Status: searching, or the totals per source.
				std::vector<std::string> searchingNames;
				for (PackSource source : discover.activeSources) {
					if (views[source].searching)
						searchingNames.push_back(PackSourceName(source));
				}
				if (!searchingNames.empty()) {
					std::string names;
					for (std::size_t i = 0; i < searchingNames.size(); ++i)
						names += (i ? ", " : "") + searchingNames[i];
					Widgets::Spinner(Theme::C().accent);
					ImGui::SameLine();
					ImGui::TextDisabled("Searching %s...", names.c_str());
				} else if (discover.activeSources.empty()) {
					Widgets::WrappedText(Theme::C().error, skipped.empty() ? "Pick at least one source."
						: ("No source can run this search. " + skipped.front()).c_str());
				} else {
					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("%zu results", discover.merged.size());
					for (PackSource source : discover.activeSources) {
						const SourceView& view = views[source];
						ImGui::SameLine(0.0f, S(14.0f));
						if (view.searchFailed) {
							ImGui::TextColored(Theme::C().error, "%s failed", PackSourceName(source));
							ImGui::SetItemTooltip("%s", view.searchStatus.c_str());
						} else {
							ImGui::TextColored(Theme::Mix(SourceColor(source), Theme::C().text, 0.3f), "%s", PackSourceName(source));
							ImGui::SameLine(0.0f, S(5.0f));
							ImGui::TextDisabled("%s", FormatCount(view.totalHits).c_str());
						}
					}
					if (!discover.skippedSources.empty()) {
						std::string why;
						for (const std::string& line : discover.skippedSources)
							why += (why.empty() ? "" : "\n") + line;
						ImGui::SameLine(0.0f, S(14.0f));
						ImGui::TextDisabled("(%zu skipped)", discover.skippedSources.size());
						ImGui::SetItemTooltip("%s", why.c_str());
					}
				}

				// Selection: keep the selected pack across re-merges, else the first result.
				Modpack* selectedPack = nullptr;
				PackSource selectedSource = PackSource::Modrinth;
				for (const auto& ref : discover.merged) {
					Modpack& item = views[ref.first].packs[ref.second];
					if (item.id == discover.selectedId) {
						selectedPack = &item;
						selectedSource = ref.first;
						break;
					}
				}
				if (!selectedPack && !discover.merged.empty()) {
					selectedSource = discover.merged.front().first;
					selectedPack = &views[selectedSource].packs[discover.merged.front().second];
					discover.selectedId = selectedPack->id;
				}

				// Pack list
				const bool browsing = discover.search[0] == '\0' && discover.gameVersion[0] == '\0' &&
					discover.loader.empty() && discover.sort == 0;
				ImGui::BeginChild("pack_list", ImVec2(S(340.0f), 0.0f), ImGuiChildFlags_Borders,
					ImGuiWindowFlags_NoScrollWithMouse);
				double rowsShownAt = pageShownAt;
				for (PackSource source : discover.activeSources)
					rowsShownAt = std::max(rowsShownAt, views[source].resultsAt);
				Widgets::SectionLabel(browsing ? (singleResult ? views[discover.activeSources.front()].caps.browseLabel : "POPULAR") : "RESULTS");
				for (std::size_t i = 0; i < discover.merged.size(); ++i) {
					const auto& ref = discover.merged[i];
					const Modpack& item = views[ref.first].packs[ref.second];
					std::string meta;
					if (item.source == PackSource::LocalIndex) {
						meta = item.version + "  \xC2\xB7  " + std::to_string(item.files.size()) + " files";
					} else {
						meta = item.gameVersion;
						if (item.downloads > 0)
							meta += (meta.empty() ? "" : "  \xC2\xB7  ") + FormatCount(item.downloads) + " downloads";
						if (meta.empty())
							meta = item.author;
					}
					ImGui::PushID(static_cast<int>(i));
					Widgets::BeginEntrance(rowsShownAt, static_cast<int>(i));
					const bool showTag = !singleResult || item.source == PackSource::LocalIndex;
					if (Widgets::PackRow(item, images, selectedPack == &item, meta,
							showTag ? (item.source == PackSource::LocalIndex ? "Offline" : PackSourceName(item.source)) : nullptr,
							SourceColor(item.source)))
						discover.selectedId = item.id;
					Widgets::EndEntrance();
					ImGui::PopID();
				}
				if (discover.merged.empty() && searchingNames.empty() && !discover.activeSources.empty())
					ImGui::TextDisabled("No modpacks found.");
				Widgets::SmoothScroll();
				ImGui::EndChild();
				ImGui::SameLine();

				// Pack details
				ImGui::BeginChild("pack_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
					ImGuiWindowFlags_NoScrollWithMouse);
				const std::string shownId = selectedPack ? selectedPack->id : std::string();
				if (shownId != detailsPackId) {
					detailsPackId = shownId;
					detailsShownAt = ImGui::GetTime();
				}
				Widgets::BeginEntrance(std::max(pageShownAt, detailsShownAt));
				if (selectedPack) {
					SourceView& view = views[selectedSource];
					const SourceCapabilities& caps = view.caps;
					const Modpack& pack = *selectedPack;
					const bool online = pack.source != PackSource::LocalIndex;
					const bool loadingFiles = filesLoading.count(pack.id) > 0;
					const auto filesError = filesErrors.find(pack.id);
					if (online && !pack.filesLoaded && !loadingFiles && filesError == filesErrors.end())
						startFetchFiles(view, pack);
					if (settings.showGalleryBanners && pack.source == PackSource::Modrinth && !pack.bannerLoaded && bannersLoading.count(pack.id) == 0)
						startFetchBanner(view, pack.id);

					Widgets::HeroBanner(pack, images, std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, S(200.0f), S(340.0f)),
						settings.showGalleryBanners);
					ImGui::Spacing();

					if (online) {
						if (pack.downloads > 0) {
							Widgets::Chip((FormatCount(pack.downloads) + " downloads").c_str(), Theme::C().accent);
							ImGui::SameLine();
						}
						if (!pack.websiteUrl.empty()) {
							std::string shown = pack.websiteUrl;
							for (const char* scheme : { "https://", "http://" }) {
								if (shown.rfind(scheme, 0) == 0)
									shown.erase(0, std::strlen(scheme));
							}
							ImGui::AlignTextToFramePadding();
							ImGui::TextLinkOpenURL(shown.c_str(), pack.websiteUrl.c_str());
						}
					}
					ImGui::Spacing();
					ImGui::PushFont(nullptr, Theme::kFontBody + 1.0f);
					ImGui::TextWrapped("%s", pack.description.c_str());
					ImGui::PopFont();
					ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

					if (caps.installsPacks && online) {
						// Whole-pack install: one card for the chosen version. Versions install side by side.
						Widgets::SectionLabel("INSTALL");
						const std::string key = "pack:" + pack.id + "@" + VersionKey(pack);
						DownloadState state;
						bool hasState = false;
						{
							std::lock_guard<std::mutex> lock(downloadsMutex);
							const auto it = downloads.find(key);
							if (it != downloads.end()) {
								state = it->second;
								hasState = true;
							}
						}
						const auto libraryEntry = libraryPacks.find(pack.id);
						const InstalledVersion* installedHere = libraryEntry != libraryPacks.end()
							? FindInstalledVersion(libraryEntry->second, VersionKey(pack)) : nullptr;
						const bool inLibrary = installedHere != nullptr;
						const std::string versionFolder = installedHere ? installedHere->gameDir
							: PlanInstalledVersion(libraryPacks, pack, true).gameDir;

						BeginCard("install_card");
						ImGui::AlignTextToFramePadding();
						if (loadingFiles) {
							Widgets::Spinner(Theme::C().accent);
							ImGui::SameLine();
							ImGui::TextDisabled("Loading latest version...");
						} else if (filesError != filesErrors.end()) {
							ImGui::TextColored(Theme::C().error, "Couldn't load versions: %s", filesError->second.c_str());
							AlignRightForButton("Retry");
							if (ImGui::Button("Retry"))
								startFetchFiles(view, pack);
						} else if (pack.filesLoaded) {
							// Version to install; the newest release is chosen at first.
							ImGui::SetNextItemWidth(S(200.0f));
							ImGui::BeginDisabled(hasState && state.active);
							PackVersionInfo picked;
							if (versionCombo("##install_version", pack, pack.version.empty() ? "Latest version" : pack.version,
									pack.sourceVersionId, picked))
								applyPickedVersion(*selectedPack, picked);
							ImGui::EndDisabled();
							ImGui::SetItemTooltip("Pick which version of the pack to install");
							ImGui::SameLine();
							std::string details;
							if (!pack.gameVersion.empty())
								details = "Minecraft " + pack.gameVersion;
							if (!pack.loaders.empty())
								details += (details.empty() ? "" : "  \xC2\xB7  ") + pack.loaders;
							ImGui::TextDisabled("%s", details.c_str());

							if (hasState && state.active) {
								std::string progressLabel = state.stage;
								if (state.stageTotal > 0)
									progressLabel += "  " + std::to_string(state.stageDone) + " / " + std::to_string(state.stageTotal);
								AlignRightForButton(progressLabel.c_str());
								ImGui::TextDisabled("%s", progressLabel.c_str());
							} else {
								const bool installed = (hasState && state.installed) || inLibrary;
								const char* label = installed ? "Reinstall" : (hasState && !state.error.empty() ? "Retry" : "Install pack");
								AlignRightForButton(label);
								if (ImGui::Button(label))
									requestPlanInstall(pack);
							}
						}
						if (hasState && state.active) {
							if (state.stageTotal > 0)
								Widgets::ThinProgressBar(static_cast<float>(static_cast<double>(state.stageDone) / static_cast<double>(state.stageTotal)));
							else
								Widgets::ThinProgressBar(0.0f);
						}
						if (hasState && !state.error.empty())
							ImGui::TextColored(Theme::C().error, "Install failed: %s", state.error.c_str());
						if (!(hasState && state.active) && ((hasState && state.installed) || inLibrary)) {
							ImGui::TextColored(Theme::C().accent, "Installed");
							ImGui::SameLine();
							ImGui::TextDisabled("to %s", versionFolder.c_str());
						}
						if (hasState && !state.blocked.empty()) {
							ImGui::Spacing();
							ImGui::TextColored(Theme::C().error, "%llu file(s) must be downloaded by hand into %s:",
								static_cast<unsigned long long>(state.blocked.size()), versionFolder.c_str());
							for (const PlannedFile& file : state.blocked) {
								ImGui::PushID(file.relPath.c_str());
								ImGui::BulletText("%s  ->  %s", file.name.c_str(), file.relPath.c_str());
								if (!file.manualPageUrl.empty()) {
									ImGui::SameLine();
									ImGui::TextLinkOpenURL("download page", file.manualPageUrl.c_str());
								}
								if (!file.blockedReason.empty())
									ImGui::TextDisabled("    %s", file.blockedReason.c_str());
								ImGui::PopID();
							}
						}
						for (const std::string& note : state.notes)
							ImGui::TextDisabled("Note: %s", note.c_str());
						EndCard();
					} else {
						char filesLabel[48];
						std::snprintf(filesLabel, sizeof(filesLabel), "PACK FILES  (%llu)", static_cast<unsigned long long>(pack.files.size()));
						Widgets::SectionLabel(filesLabel);
						if (loadingFiles) {
							Widgets::Spinner(Theme::C().accent);
							ImGui::SameLine();
							ImGui::TextDisabled("Loading latest version...");
						} else if (filesError != filesErrors.end()) {
							ImGui::TextColored(Theme::C().error, "Couldn't load versions: %s", filesError->second.c_str());
							if (ImGui::Button("Retry"))
								startFetchFiles(view, pack);
						}
						if (online && pack.filesLoaded) {
							// Which version's .mrpack to download; the newest release is chosen at first.
							ImGui::AlignTextToFramePadding();
							ImGui::TextDisabled("Version");
							ImGui::SameLine();
							ImGui::SetNextItemWidth(S(220.0f));
							PackVersionInfo picked;
							if (versionCombo("##file_version", pack, pack.version.empty() ? "Latest version" : pack.version,
									pack.sourceVersionId, picked))
								applyPickedVersion(*selectedPack, picked);
							ImGui::SameLine();
							std::string details = pack.gameVersion.empty() ? std::string() : "Minecraft " + pack.gameVersion;
							if (!pack.loaders.empty())
								details += (details.empty() ? "" : "  \xC2\xB7  ") + pack.loaders;
							ImGui::TextDisabled("%s", details.c_str());
						}

						for (const PackFile& file : pack.files) {
							DownloadState state;
							bool hasState = false;
							{
								std::lock_guard<std::mutex> lock(downloadsMutex);
								const auto it = downloads.find(file.url);
								if (it != downloads.end()) {
									state = it->second;
									hasState = true;
								}
							}

							ImGui::PushID(file.url.c_str());
							BeginCard("file_card");
							ImGui::AlignTextToFramePadding();
							ImGui::PushFont(fonts.bold, 0.0f);
							ImGui::TextUnformatted(file.name.c_str());
							ImGui::PopFont();
							ImGui::SameLine();
							ImGui::TextDisabled("%s", FormatFileSize(file.size).c_str());

							if (!hasState || (!state.active && state.outPath.empty())) {
								// One step: download, then extract into installs/<pack id>/ right away.
								const char* label = hasState ? "Retry" : "Install";
								AlignRightForButton(label);
								if (ImGui::Button(label))
									startDownloadInstall(pack, file);
								if (hasState && !state.error.empty())
									ImGui::TextColored(Theme::C().error, "Download failed: %s", state.error.c_str());
							} else if (state.active) {
								char progressLabel[64];
								if (state.total > 0.0)
									std::snprintf(progressLabel, sizeof(progressLabel), "Downloading  %.0f%%", 100.0 * state.now / state.total);
								else
									std::snprintf(progressLabel, sizeof(progressLabel), "Downloading");
								AlignRightForButton(progressLabel);
								ImGui::AlignTextToFramePadding();
								ImGui::TextDisabled("%s", progressLabel);
							} else if (state.installing) {
								AlignRightForButton("Extracting");
								ImGui::AlignTextToFramePadding();
								ImGui::TextDisabled("Extracting");
							} else if (state.installed) {
								AlignRightForButton("Installed");
								ImGui::AlignTextToFramePadding();
								ImGui::TextColored(Theme::C().accent, "Installed");
							} else if (!state.outPath.empty()) {
								// Downloaded earlier but not extracted (an older session, or extraction failed).
								AlignRightForButton("Install");
								if (ImGui::Button("Install"))
									startInstall(file.url, pack, state.outPath);
							}

							if (state.active && state.total > 0.0)
								Widgets::ThinProgressBar(static_cast<float>(state.now / state.total));
							else if (state.installing)
								Widgets::ThinProgressBar(static_cast<float>(state.installProgress));
							if (!state.installError.empty())
								ImGui::TextColored(Theme::C().error, "Extraction failed: %s", state.installError.c_str());
							EndCard();
							ImGui::PopID();
						}

					}

					ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
					ImGui::PushFont(fonts.bold, Theme::kFontHeading);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
					const auto libraryPack = libraryPacks.find(pack.id);
					bool playBusy = false;
					{
						std::lock_guard<std::mutex> lock(playsMutex);
						const auto it = plays.find(pack.id);
						playBusy = it != plays.end() && it->second.busy;
					}
					// Plays the version shown here when it's installed (making it the active one),
					// else the pack's active version.
					const InstalledVersion* shownInstalled = libraryPack != libraryPacks.end() && pack.filesLoaded
						? FindInstalledVersion(libraryPack->second, VersionKey(pack)) : nullptr;
					ImGui::BeginDisabled(libraryPack == libraryPacks.end() || playBusy);
					if (ImGui::Button(playBusy ? "PLAYING" : "PLAY", ImVec2(-1.0f, S(52.0f)))) {
						if (shownInstalled && shownInstalled->key != libraryPack->second.activeVersion) {
							const std::string key = shownInstalled->key;
							SelectInstalledVersion(libraryPack->second, key);
							if (!SavePackLibrary(kLibraryFile, libraryPacks))
								log << "Could not save " << kLibraryFile << "\n";
						}
						startPlay(libraryPack->second);
						page = Page::Library;
					}
					ImGui::EndDisabled();
					ImGui::PopStyleVar();
					ImGui::PopFont();
					if (libraryPack == libraryPacks.end())
						ImGui::TextDisabled(GetSourceCapabilities(pack.source).installsPacks
							? "Install the pack to play it." : "Install the pack's file to play it.");
					else if (!shownInstalled && pack.filesLoaded)
						ImGui::TextDisabled("This version isn't installed. PLAY starts your installed %s.",
							libraryPack->second.version.c_str());
					else
						ImGui::TextDisabled("Progress and errors appear in My Library.");

					// What's in the pack.
					ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
					if (online && ImGui::BeginTabBar("pack_tabs")) {
						if (ImGui::BeginTabItem("Description")) {
							const auto description = packDescriptions.find(pack.id);
							const auto descriptionError = descriptionErrors.find(pack.id);
							const bool loadingDescription = descriptionsLoading.count(pack.id) > 0;
							if (description == packDescriptions.end() && !loadingDescription && descriptionError == descriptionErrors.end())
								startFetchDescription(pack);
							if (loadingDescription) {
								Widgets::Spinner(Theme::C().accent);
								ImGui::SameLine();
								ImGui::TextDisabled("Loading description...");
							} else if (descriptionError != descriptionErrors.end()) {
								Widgets::WrappedText(Theme::C().error, ("Couldn't load the description: " + descriptionError->second).c_str());
								if (ImGui::Button("Retry##description"))
									startFetchDescription(pack);
							} else if (description != packDescriptions.end()) {
								ImGui::PushFont(nullptr, Theme::kFontBody + 1.0f);
								ImGui::PushTextWrapPos(0.0f);
								ImGui::TextUnformatted(description->second.empty() ? "This pack has no description."
									: description->second.c_str());
								ImGui::PopTextWrapPos();
								ImGui::PopFont();
							}
							ImGui::EndTabItem();
						}
						const std::string key = modsKey(pack);
						const auto list = packMods.find(key);
						const std::string modsTab = list != packMods.end() && !list->second.mods.empty()
							? "Mods (" + std::to_string(list->second.mods.size()) + ")###mods" : std::string("Mods###mods");
						if (ImGui::BeginTabItem(modsTab.c_str())) {
							const bool loadingMods = modsLoading.count(key) > 0;
							const auto modsError = modsErrors.find(key);
							// CurseForge only lists mods in the pack zip, so it waits for a click.
							const bool needsClick = pack.source == PackSource::CurseForge;
							if (pack.filesLoaded && !loadingMods && list == packMods.end() && modsError == modsErrors.end() &&
								!needsClick && !pack.sourceVersionId.empty())
								startFetchMods(pack);
							if (!pack.filesLoaded || loadingMods) {
								Widgets::Spinner(Theme::C().accent);
								ImGui::SameLine();
								ImGui::TextDisabled(!pack.filesLoaded ? "Loading the pack's version..."
									: needsClick ? "Downloading the pack's manifest..." : "Loading mod list...");
							} else if (modsError != modsErrors.end()) {
								Widgets::WrappedText(Theme::C().error, ("Couldn't load the mod list: " + modsError->second).c_str());
								if (ImGui::Button("Retry##mods"))
									startFetchMods(pack);
							} else if (list == packMods.end()) {
								ImGui::TextDisabled("CurseForge lists a pack's mods in its archive.");
								if (ImGui::Button("Load mod list"))
									startFetchMods(pack);
								ImGui::SetItemTooltip("Downloads the pack's archive to read its manifest. Installing reuses it.");
							} else {
								const PackModList& mods = list->second;
								if (!mods.note.empty())
									ImGui::TextDisabled("%s", mods.note.c_str());
								if (mods.mods.empty()) {
									if (mods.note.empty())
										ImGui::TextDisabled("This version doesn't list any mods.");
								} else {
									ImGui::SetNextItemWidth(-FLT_MIN);
									ImGui::InputTextWithHint("##mods_filter", "Filter mods...", &modsFilter);
									const std::string filter = ToLower(modsFilter);
									int shown = 0;
									for (std::size_t i = 0; i < mods.mods.size(); ++i) {
										const PackMod& mod = mods.mods[i];
										if (!filter.empty() && !ContainsIgnoreCase(mod.name, filter) && !ContainsIgnoreCase(mod.summary, filter))
											continue;
										++shown;
										ImGui::PushID(static_cast<int>(i));
										ImGui::PushFont(fonts.bold, 0.0f);
										if (!mod.url.empty())
											ImGui::TextLinkOpenURL(mod.name.c_str(), mod.url.c_str());
										else
											ImGui::TextUnformatted(mod.name.c_str());
										ImGui::PopFont();
										if (!mod.version.empty() && mod.version != mod.name) {
											ImGui::SameLine();
											ImGui::TextDisabled("%s", mod.version.c_str());
										}
										auto tag = [&](const char* text, const ImVec4& tint) {
											ImGui::SameLine();
											Widgets::Chip(text, tint);
										};
										if (mod.clientOnly)
											tag("Client only", Theme::C().info);
										if (mod.serverOnly)
											tag("Server only", Theme::C().info);
										if (mod.optional)
											tag("Optional", Theme::C().textDim);
										if (!mod.summary.empty()) {
											ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().textDim);
											ImGui::TextWrapped("%s", mod.summary.c_str());
											ImGui::PopStyleColor();
										}
										ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
										ImGui::PopID();
									}
									if (shown == 0)
										ImGui::TextDisabled("No mods match the filter.");
								}
							}
							ImGui::EndTabItem();
						}
						ImGui::EndTabBar();
					}
				} else if (searchingNames.empty()) {
					ImGui::TextDisabled("No modpacks match your search.");
				}
				Widgets::EndEntrance();
				Widgets::SmoothScroll();
				ImGui::EndChild();
			} else if (page == Page::Library) {
				const float iconSize = S(56.0f);
				int cardIndex = 0;
				for (auto& [packId, pack] : libraryPacks) {
					PlayState play;
					{
						std::lock_guard<std::mutex> lock(playsMutex);
						const auto it = plays.find(packId);
						if (it != plays.end())
							play = it->second;
					}
					Widgets::BeginEntrance(pageShownAt, cardIndex++);
					BeginCard(pack.id.c_str());
					PackIconItem(pack, images, iconSize);
					ImGui::SameLine(0.0f, S(14.0f));
					ImGui::BeginGroup();
					const bool hostable = canHostServer(pack);
					const auto runningUpdate = packUpdates.find(packId);
					const bool updating = runningUpdate != packUpdates.end();
					ImGui::PushTextWrapPos(0.0f);
					Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::C().text, pack.name.c_str());
					// With several installed versions, a dropdown picks the active one (what PLAY,
					// Open folder and Manage act on).
					const bool multipleVersions = pack.installedVersions.size() > 1;
					std::string details = PackSourceName(pack.source);
					if (!multipleVersions)
						details += "  \xC2\xB7  " + pack.version;
					if (!pack.gameVersion.empty())
						details += "  \xC2\xB7  Minecraft " + pack.gameVersion;
					if (!pack.loaders.empty())
						details += "  \xC2\xB7  " + pack.loaders;
					std::string switchTo;
					if (multipleVersions) {
						ImGui::SetNextItemWidth(S(190.0f));
						ImGui::BeginDisabled(play.busy || updating);
						if (ImGui::BeginCombo("##installed_version", pack.version.c_str())) {
							for (const InstalledVersion& v : pack.installedVersions) {
								ImGui::PushID(v.key.c_str());
								if (ImGui::Selectable(v.version.empty() ? v.key.c_str() : v.version.c_str(), v.key == pack.activeVersion))
									switchTo = v.key;
								if (!v.gameVersion.empty()) {
									ImGui::SameLine();
									ImGui::TextDisabled("Minecraft %s", v.gameVersion.c_str());
								}
								ImGui::PopID();
							}
							ImGui::EndCombo();
						}
						ImGui::EndDisabled();
						ImGui::SetItemTooltip("%d versions installed. PLAY starts the one picked here; each has its own "
							"mods, settings and worlds. Install more from Discover.", static_cast<int>(pack.installedVersions.size()));
						ImGui::SameLine();
						ImGui::AlignTextToFramePadding();
					}
					ImGui::TextDisabled("%s", details.c_str());
					if (!pack.description.empty())
						Widgets::WrappedText(Theme::C().textDim, ShortenText(pack.description, 160).c_str());
					if (!switchTo.empty()) {
						SelectInstalledVersion(pack, switchTo);
						if (!SavePackLibrary(kLibraryFile, libraryPacks))
							log << "Could not save " << kLibraryFile << "\n";
					}
					if (play.playing) {
						ImGui::TextColored(Theme::C().accent, "Minecraft is running");
					} else if (updating) {
						Widgets::Spinner(Theme::C().accent);
						ImGui::SameLine();
						ImGui::TextDisabled(runningUpdate->second.copying ? "Copying your worlds and settings to %s"
							: "Updating to %s (progress is on the Downloads page)", runningUpdate->second.toVersion.c_str());
					} else if (play.busy) {
						Widgets::Spinner(Theme::C().accent);
						ImGui::SameLine();
						ImGui::TextDisabled("%s", play.step.c_str());
					} else if (!play.error.empty()) {
						ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().error);
						ImGui::TextWrapped("%s", play.error.c_str());
						ImGui::PopStyleColor();
					} else {
						ImGui::TextDisabled("Ready to play");
					}
					ImGui::EndGroup();
					ImGui::PopTextWrapPos();

					// Actions, right-aligned as one group so nothing runs past the card's edge.
					ImGui::PushFont(fonts.bold, 0.0f);
					if (play.playing) {
						AlignRightForButtons({ "Stop game" });
						ImGui::PushStyleColor(ImGuiCol_Button, Theme::Mix(Theme::C().cardBg, Theme::C().error, 0.55f));
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Mix(Theme::C().cardBg, Theme::C().error, 0.75f));
						if (ImGui::Button("Stop game"))
							stopConfirmPackId = pack.id;
						ImGui::PopStyleColor(2);
						ImGui::SetItemTooltip("Force-close Minecraft");
					} else if (play.busy) {
						AlignRightForButtons({ "Cancel" });
						if (ImGui::Button("Cancel"))
							play.cancel->store(true);
					} else {
						AlignRightForButtons(hostable ? std::vector<const char*>{ "Server", "Open folder", "Manage", "  PLAY  " }
							: std::vector<const char*>{ "Open folder", "Manage", "  PLAY  " });
						if (hostable) {
							if (ImGui::Button("Server")) {
								serverPackId = pack.id;
								page = Page::Servers;
							}
							ImGui::SetItemTooltip("Host this pack as a server for your friends");
							ImGui::SameLine();
						}
						if (ImGui::Button("Open folder"))
							Platform::OpenFolder(gameDir(pack).string());
						ImGui::SetItemTooltip("Open the game folder (mods, config, saves, logs)");
						ImGui::SameLine();
						ImGui::BeginDisabled(updating);
						if (ImGui::Button("Manage"))
							ImGui::OpenPopup("manage_pack");
						ImGui::EndDisabled();
						if (ImGui::BeginPopup("manage_pack")) {
							ImGui::PushFont(nullptr, 0.0f);
							const fs::path logsDir = gameDir(pack) / "logs";
							std::error_code logError;
							const bool hasLog = fs::is_regular_file(logsDir / "latest.log", logError);
							if (ImGui::MenuItem("Open latest log", nullptr, false, hasLog))
								Platform::OpenFile((logsDir / "latest.log").string());
							if (ImGui::MenuItem("Open logs folder"))
								Platform::OpenFolder(logsDir.string());
							ImGui::Separator();
							if (ImGui::MenuItem("Launch settings...", nullptr, settings.packLaunch.count(pack.id) > 0))
								launchSettingsPackId = pack.id;
							ImGui::SetItemTooltip("Memory, Java and window options for this pack only");
							if (pack.source != PackSource::LocalIndex) {
								if (ImGui::MenuItem("Update..."))
									startUpdateCheck(pack);
								ImGui::SetItemTooltip("Install the newest version from %s and bring your worlds and settings along",
									PackSourceName(pack.source));
							}
							if (ImGui::MenuItem("Repair files"))
								startRepair(pack);
							ImGui::SetItemTooltip("Re-check every pack file and re-download damaged or missing ones");
							ImGui::Separator();
							if (pack.installedVersions.size() > 1) {
								if (ImGui::MenuItem(("Uninstall " + pack.version + "...").c_str())) {
									uninstallPackId = pack.id;
									uninstallVersionKey = pack.activeVersion;
									uninstallDeleteServer = false;
								}
								if (ImGui::MenuItem("Uninstall all versions...")) {
									uninstallPackId = pack.id;
									uninstallVersionKey.clear();
									uninstallDeleteServer = false;
								}
							} else if (ImGui::MenuItem("Uninstall...")) {
								uninstallPackId = pack.id;
								uninstallVersionKey.clear();
								uninstallDeleteServer = false;
							}
							ImGui::PopFont();
							ImGui::EndPopup();
						}
						ImGui::SameLine();
						ImGui::PushStyleColor(ImGuiCol_Button, Theme::C().accent);
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Mix(Theme::C().accent, Theme::C().text, 0.15f));
						ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().windowBg);
						ImGui::BeginDisabled(updating);
						if (ImGui::Button("  PLAY  "))
							startPlay(pack);
						ImGui::EndDisabled();
						ImGui::PopStyleColor(3);
					}
					ImGui::PopFont();
					if (play.busy && !play.playing && play.progress >= 0.0)
						Widgets::ThinProgressBar(static_cast<float>(play.progress));
					EndCard();
					Widgets::EndEntrance();
				}
				if (libraryPacks.empty() && Widgets::EmptyState("Your library is empty",
						"Packs you download and install from Discover show up here.", "Browse modpacks"))
					page = Page::Discover;
			} else if (page == Page::Servers) {
				// Keep server states in step with their processes.
				for (auto& [packId, server] : servers) {
					const bool running = server.process && server.process->Running();
					if (server.running && !running) {
						const int code = server.process ? server.process->ExitCode() : 0;
						Toasts::Push(code == 0 ? Toasts::Kind::Info : Toasts::Kind::Error, "Server stopped",
							code == 0 ? libraryPacks.count(packId) ? libraryPacks.at(packId).name : packId
								: "It exited with code " + std::to_string(code) + ". See the console for details.");
						server.stopRequestedAt = -1.0;
					}
					server.running = running;
				}

				// Library packs, plus servers whose pack was uninstalled.
				std::map<std::string, const Modpack*> serverPacks;
				for (const auto& [packId, pack] : libraryPacks)
					serverPacks[packId] = &pack;
				for (const auto& [packId, pack] : orphanServers) {
					if (!serverPacks.count(packId))
						serverPacks[packId] = &pack;
				}
				if (serverPacks.empty()) {
					if (Widgets::EmptyState("No servers yet",
							"Install a modpack first. Then you can turn it into a server for your friends here.", "Browse modpacks"))
						page = Page::Discover;
				} else {
					if (!serverPacks.count(serverPackId))
						serverPackId = serverPacks.begin()->first;

					// Server list: every library pack, with its server status.
					ImGui::BeginChild("server_list", ImVec2(S(320.0f), 0.0f), ImGuiChildFlags_Borders);
					Widgets::SectionLabel("YOUR PACKS");
					for (const auto& [packId, packPtr] : serverPacks) {
						const Modpack& pack = *packPtr;
						const bool inLibrary = libraryPacks.count(packId) > 0;
						const fs::path dir = fs::path(kServersRoot) / packId;
						ServerState* serverEntry;
						{
							// The setup worker looks servers up; inserting must not race with that.
							std::lock_guard<std::mutex> lock(serversMutex);
							serverEntry = &servers[packId];
						}
						ServerState& server = *serverEntry;
						if (!server.loaded) {
							server.loaded = true;
							server.ready = LoadServerInfo(dir, server.info);
							server.props = ReadServerProperties(dir);
							server.eula = EulaAccepted(dir);
							server.memoryMb = LoadServerMemory(dir, std::max(4096, settings.launch.maxMemoryMb));
						}
						bool busy;
						{
							std::lock_guard<std::mutex> lock(serversMutex);
							busy = server.busy;
						}
						const char* tag = server.running ? "Running" : busy ? "Setting up" : server.ready ? "Ready" : nullptr;
						const ImVec4 tagColor = server.running ? Theme::C().accent : busy ? Theme::C().info : Theme::C().textDim;
						const std::string meta = !inLibrary ? "Pack uninstalled"
							: canHostServer(pack)
							? (server.ready ? server.info.loader + " " + server.info.minecraftVersion : "No server yet")
							: "Pack files are missing";
						ImGui::PushID(packId.c_str());
						if (Widgets::PackRow(pack, images, packId == serverPackId, meta, tag, tagColor))
							serverPackId = packId;
						ImGui::PopID();
					}
					ImGui::EndChild();
					ImGui::SameLine();

					// Details for the selected pack's server.
					ImGui::BeginChild("server_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
					const Modpack& pack = *serverPacks.at(serverPackId);
					ServerState* serverEntry;
					{
						std::lock_guard<std::mutex> lock(serversMutex);
						serverEntry = &servers[serverPackId];
					}
					ServerState& server = *serverEntry;
					const fs::path serverDir = fs::path(kServersRoot) / serverPackId;
					const bool inLibrary = libraryPacks.count(serverPackId) > 0;
					// supported: the pack can be (re)built into a server. A finished server can still
					// be run and configured after its pack was uninstalled.
					const bool supported = inLibrary && canHostServer(pack);
					std::error_code serverDirError;
					const bool hasServerDir = fs::is_directory(serverDir, serverDirError);
					bool busy, deleting;
					std::string step, setupError;
					double progress;
					{
						std::lock_guard<std::mutex> lock(serversMutex);
						busy = server.busy;
						deleting = server.deleting;
						step = server.step;
						progress = server.progress;
						setupError = server.error;
					}

					PackIconItem(pack, images, S(56.0f));
					ImGui::SameLine(0.0f, S(14.0f));
					ImGui::BeginGroup();
					Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::C().text, pack.name.c_str());
					if (server.running)
						ImGui::TextColored(Theme::C().accent, "Running");
					else if (busy)
						ImGui::TextDisabled("Setting up...");
					else if (server.ready)
						ImGui::TextDisabled("%s %s  \xC2\xB7  Minecraft %s  \xC2\xB7  stopped", server.info.loader.c_str(),
							server.info.loaderVersion.c_str(), server.info.minecraftVersion.c_str());
					else
						ImGui::TextDisabled("No server yet");
					ImGui::EndGroup();
					ImGui::Spacing();

					if (!supported && !server.ready) {
						Widgets::WrappedText(Theme::C().textDim, !inLibrary
							? "This server's pack was uninstalled, and its server files are incomplete."
							: "This pack's files are missing or incomplete, so it can't be turned into a server. "
								"Repair or reinstall it from My Library.");
						if (hasServerDir) {
							ImGui::Spacing();
							ImGui::BeginDisabled(busy);
							if (ImGui::Button("Delete server..."))
								deleteServerPackId = serverPackId;
							ImGui::EndDisabled();
							if (busy) {
								ImGui::SameLine();
								Widgets::Spinner(Theme::C().accent);
								ImGui::SameLine();
								ImGui::TextDisabled("%s", step.c_str());
							}
							if (!setupError.empty())
								Widgets::WrappedText(Theme::C().error, setupError.c_str());
						}
					} else {
						// 1. Server files.
						BeginCard("server_files");
						Widgets::SectionLabel("SERVER FILES");
						if (busy) {
							Widgets::Spinner(Theme::C().accent);
							ImGui::SameLine();
							ImGui::TextDisabled("%s", step.c_str());
							if (!deleting) {
								AlignRightForButton("Cancel");
								if (ImGui::Button("Cancel##setup")) {
									std::lock_guard<std::mutex> lock(serversMutex);
									server.cancel->store(true);
								}
							}
							if (progress >= 0.0)
								Widgets::ThinProgressBar(static_cast<float>(progress));
						} else {
							// Which pack version to set up: the one installed in My Library unless another is picked.
							if (pack.source != PackSource::LocalIndex) {
								ImGui::AlignTextToFramePadding();
								ImGui::TextDisabled("Pack version");
								ImGui::SameLine();
								ImGui::SetNextItemWidth(S(240.0f));
								std::string chosenId;
								std::string preview;
								{
									std::lock_guard<std::mutex> lock(serversMutex);
									chosenId = server.hasVersion ? server.version.sourceVersionId : pack.sourceVersionId;
									preview = server.hasVersion ? server.version.versionNumber : pack.version + " (installed)";
								}
								ImGui::BeginDisabled(server.running);
								PackVersionInfo picked;
								if (versionCombo("##server_version", pack, preview, chosenId, picked)) {
									std::lock_guard<std::mutex> lock(serversMutex);
									server.hasVersion = !isInstalledVersion(pack, picked);
									server.version = picked;
								}
								ImGui::EndDisabled();
								if (server.ready && !server.info.packVersion.empty()) {
									ImGui::SameLine();
									ImGui::TextDisabled("Server has %s", server.info.packVersion.c_str());
								}
							}
							const char* label = server.ready ? "Update server files" : "Create server files";
							std::vector<const char*> buttons;
							if (hasServerDir) {
								buttons.push_back("Open folder");
								buttons.push_back("Delete...");
							}
							if (supported)
								buttons.push_back(label);
							// Wrap the description short of the buttons on its right.
							float buttonsWidth = 0.0f;
							for (const char* button : buttons)
								buttonsWidth += ImGui::CalcTextSize(button).x + ImGui::GetStyle().FramePadding.x * 2.0f +
									ImGui::GetStyle().ItemSpacing.x;
							ImGui::AlignTextToFramePadding();
							ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonsWidth - S(16.0f));
							if (!inLibrary)
								ImGui::TextDisabled("Installed in servers/%s. Its pack was uninstalled, so it can't be updated.", serverPackId.c_str());
							else
								ImGui::TextDisabled(server.ready ? "Installed in servers/%s" : "Downloads the pack's server mods, its loader's server and Java.",
									serverPackId.c_str());
							ImGui::PopTextWrapPos();
							AlignRightForButtons(buttons);
							if (hasServerDir) {
								if (ImGui::Button("Open folder##server"))
									Platform::OpenFolder(serverDir.string());
								ImGui::SameLine();
								ImGui::BeginDisabled(server.running);
								if (ImGui::Button("Delete..."))
									deleteServerPackId = serverPackId;
								ImGui::EndDisabled();
								ImGui::SetItemTooltip(server.running ? "Stop the server first"
									: "Delete this server's files, world included");
								if (supported)
									ImGui::SameLine();
							}
							if (supported) {
								ImGui::BeginDisabled(server.running);
								if (ImGui::Button(label))
									startServerSetup(pack);
								ImGui::EndDisabled();
								if (server.running)
									ImGui::SetItemTooltip("Stop the server first");
							}
							if (!setupError.empty())
								Widgets::WrappedText(Theme::C().error, setupError.c_str());
							for (const std::string& note : server.info.notes)
								Widgets::WrappedText(Theme::C().textDim, ("Note: " + note).c_str());
						}
						EndCard();
						ImGui::Spacing();

						// 2. Settings (server.properties and memory).
						BeginCard("server_settings");
						Widgets::SectionLabel("SETTINGS");
						ServerProperties& props = server.props;
						bool propsChanged = false;
						const float labelWidth = S(170.0f);
						auto label = [&](const char* text) {
							ImGui::AlignTextToFramePadding();
							ImGui::TextUnformatted(text);
							ImGui::SameLine(labelWidth);
							ImGui::SetNextItemWidth(-FLT_MIN);
						};
						label("Message of the day");
						propsChanged |= ImGui::InputText("##motd", &props.motd);
						label("Port");
						if (ImGui::InputInt("##port", &props.port, 0)) {
							props.port = std::clamp(props.port, 1024, 65535);
							propsChanged = true;
						}
						label("Host IP");
						{
							auto adapterOf = [&](const std::string& address) -> const Platform::NetworkAddress* {
								for (const Platform::NetworkAddress& item : hostAddresses) {
									if (item.address == address)
										return &item;
								}
								return nullptr;
							};
							const Platform::NetworkAddress* current = adapterOf(props.serverIp);
							const bool custom = server.hostIpCustom || (!props.serverIp.empty() && !current);
							std::string preview = custom ? "Custom address" : props.serverIp.empty() ? "All network adapters"
								: props.serverIp + "  (" + current->adapter + ")";
							if (ImGui::BeginCombo("##host_ip", preview.c_str())) {
								if (Widgets::ListItem("All network adapters", !custom && props.serverIp.empty())) {
									server.hostIpCustom = false;
									props.serverIp.clear();
									propsChanged = true;
								}
								for (const Platform::NetworkAddress& item : hostAddresses) {
									const std::string text = item.address + "  (" + item.adapter + ")";
									if (Widgets::ListItem(text.c_str(), !custom && props.serverIp == item.address)) {
										server.hostIpCustom = false;
										props.serverIp = item.address;
										propsChanged = true;
									}
								}
								if (Widgets::ListItem("Custom address...", custom)) {
									server.hostIpCustom = true;
									server.hostIpEdit = props.serverIp;
								}
								ImGui::EndCombo();
							}
							ImGui::SetItemTooltip("The address the server listens on. Pick one adapter, such as a VPN like "
								"Tailscale or ZeroTier, to accept players only through it.");
							if (custom) {
								if (!server.hostIpCustom) {
									server.hostIpCustom = true;
									server.hostIpEdit = props.serverIp;
								}
								ImGui::Dummy(ImVec2(0.0f, 0.0f));
								ImGui::SameLine(labelWidth);
								ImGui::SetNextItemWidth(-FLT_MIN);
								if (ImGui::InputTextWithHint("##host_ip_custom", "e.g. 192.168.1.20", &server.hostIpEdit) &&
									IsValidServerIp(server.hostIpEdit) && server.hostIpEdit != props.serverIp) {
									props.serverIp = server.hostIpEdit;
									propsChanged = true;
								}
								ImGui::Dummy(ImVec2(0.0f, 0.0f));
								ImGui::SameLine(labelWidth);
								if (!IsValidServerIp(server.hostIpEdit))
									ImGui::TextColored(Theme::C().error, "Enter an IPv4 address like 192.168.1.20, or leave it empty.");
								else if (!props.serverIp.empty() && !adapterOf(props.serverIp) && props.serverIp != "127.0.0.1")
									ImGui::TextColored(Theme::C().error, "No adapter on this PC has this address, so the server won't start.");
								else
									ImGui::TextDisabled("Empty listens on every adapter.");
							}
						}
						label("Max players");
						propsChanged |= ImGui::SliderInt("##players", &props.maxPlayers, 1, 100);
						label("Game mode");
						const char* modes[] = { "survival", "creative", "adventure", "spectator" };
						int mode = 0;
						for (int i = 0; i < 4; ++i) if (props.gamemode == modes[i]) mode = i;
						if (ImGui::Combo("##gamemode", &mode, "Survival\0Creative\0Adventure\0Spectator\0")) {
							props.gamemode = modes[mode];
							propsChanged = true;
						}
						label("Difficulty");
						const char* difficulties[] = { "peaceful", "easy", "normal", "hard" };
						int difficulty = 2;
						for (int i = 0; i < 4; ++i) if (props.difficulty == difficulties[i]) difficulty = i;
						if (ImGui::Combo("##difficulty", &difficulty, "Peaceful\0Easy\0Normal\0Hard\0")) {
							props.difficulty = difficulties[difficulty];
							propsChanged = true;
						}
						label("View distance");
						propsChanged |= ImGui::SliderInt("##view", &props.viewDistance, 3, 32, "%d chunks");
						label("PvP");
						propsChanged |= Widgets::Toggle("##pvp", &props.pvp);
						label("Whitelist");
						propsChanged |= Widgets::Toggle("##whitelist", &props.whitelist);
						ImGui::SameLine(0.0f, S(12.0f));
						ImGui::TextDisabled("Only players you add with \"whitelist add <name>\" can join");
						label("Online mode");
						propsChanged |= Widgets::Toggle("##online", &props.onlineMode);
						ImGui::SameLine(0.0f, S(12.0f));
						if (props.onlineMode)
							ImGui::TextDisabled("Players need a Microsoft account that owns Minecraft");
						else
							ImGui::TextColored(Theme::C().error, "Anyone can join with any name. Use a whitelist.");
						label("Memory");
						float memoryGb = server.memoryMb / 1024.0f;
						if (ImGui::SliderFloat("##server_memory", &memoryGb, 1.0f,
								std::max(2.0f, Platform::SystemMemoryMb() / 1024.0f), "%.1f GB")) {
							server.memoryMb = std::max(1024, static_cast<int>(std::lround(memoryGb * 2.0f)) * 512);
						}
						if (ImGui::IsItemDeactivatedAfterEdit())
							SaveServerMemory(serverDir, server.memoryMb);
						if (propsChanged) {
							std::string error;
							if (!WriteServerProperties(serverDir, props, &error))
								log << error << "\n";
						}
						if (server.running)
							ImGui::TextDisabled("Changes apply the next time the server starts.");
						EndCard();
						ImGui::Spacing();

						// 3. Mods: turn single mods off, e.g. one that crashes the server.
						if (!server.modsLoaded) {
							server.mods = ListServerMods(serverDir);
							server.modsLoaded = true;
						}
						if (!server.mods.empty()) {
							BeginCard("server_mods");
							Widgets::SectionLabel("MODS");
							const auto disabledCount = std::count_if(server.mods.begin(), server.mods.end(),
								[](const ServerMod& mod) { return !mod.enabled; });
							ImGui::AlignTextToFramePadding();
							ImGui::TextDisabled("%zu mods, %lld turned off. Mods you turn off stay off when the server files are updated.",
								server.mods.size(), static_cast<long long>(disabledCount));
							AlignRightForButton("Refresh");
							if (ImGui::Button("Refresh##mods")) {
								server.modsLoaded = false;
								server.modError.clear();
							}
							ImGui::SetItemTooltip("Re-read the server's mods folder");
							ImGui::SetNextItemWidth(-FLT_MIN);
							ImGui::InputTextWithHint("##mod_filter", "Filter mods...", &server.modFilter);
							const std::string filter = ToLower(server.modFilter);
							std::vector<std::size_t> shown;
							for (std::size_t i = 0; i < server.mods.size(); ++i) {
								if (filter.empty() || ContainsIgnoreCase(server.mods[i].fileName, filter))
									shown.push_back(i);
							}
							const float rowHeight = ImGui::GetFrameHeightWithSpacing();
							const float listHeight = std::min(S(280.0f),
								rowHeight * static_cast<float>(std::max<std::size_t>(shown.size(), 1)) + ImGui::GetStyle().WindowPadding.y * 2.0f);
							ImGui::BeginChild("server_mod_list", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders);
							ImGui::BeginDisabled(server.running || busy);
							ImGuiListClipper clipper;
							clipper.Begin(static_cast<int>(shown.size()), rowHeight);
							while (clipper.Step()) {
								for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
									ServerMod& mod = server.mods[shown[static_cast<std::size_t>(row)]];
									ImGui::PushID(mod.fileName.c_str());
									bool on = mod.enabled;
									if (Widgets::Toggle("##mod_on", &on)) {
										std::string error;
										if (SetServerModEnabled(serverDir, mod.fileName, on, &error)) {
											mod.enabled = on;
											server.modError.clear();
											log << "Server " << serverPackId << ": " << (on ? "enabled " : "disabled ") << mod.fileName << "\n";
										} else {
											server.modError = error;
										}
									}
									ImGui::SameLine(0.0f, S(10.0f));
									ImGui::AlignTextToFramePadding();
									if (mod.enabled)
										ImGui::TextUnformatted(mod.fileName.c_str());
									else
										ImGui::TextDisabled("%s", mod.fileName.c_str());
									ImGui::SameLine();
									ImGui::TextDisabled("%s", FormatFileSize(mod.size).c_str());
									ImGui::PopID();
								}
							}
							if (shown.empty())
								ImGui::TextDisabled("No mods match the filter.");
							ImGui::EndDisabled();
							ImGui::EndChild();
							if (server.running)
								ImGui::TextDisabled("Stop the server to turn mods on or off.");
							if (!server.modError.empty())
								Widgets::WrappedText(Theme::C().error, server.modError.c_str());
							EndCard();
							ImGui::Spacing();
						}

						// 4. Run.
						BeginCard("server_run");
						Widgets::SectionLabel("RUN");
						bool eula = server.eula;
						ImGui::BeginDisabled(server.eula);
						if (Widgets::Toggle("##eula", &eula) && eula) {
							std::string error;
							if (AcceptEula(serverDir, &error))
								server.eula = true;
							else
								log << error << "\n";
						}
						ImGui::EndDisabled();
						ImGui::SameLine(0.0f, S(12.0f));
						ImGui::AlignTextToFramePadding();
						ImGui::TextUnformatted("I agree to the");
						ImGui::SameLine(0.0f, S(4.0f));
						ImGui::TextLinkOpenURL("Minecraft EULA", "https://aka.ms/MinecraftEULA");
						ImGui::PushFont(fonts.bold, 0.0f);
						if (!server.running) {
							ImGui::BeginDisabled(!server.ready || !server.eula || busy);
							ImGui::PushStyleColor(ImGuiCol_Button, Theme::C().accent);
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Mix(Theme::C().accent, Theme::C().text, 0.15f));
							ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().windowBg);
							if (ImGui::Button("  Start server  ", ImVec2(0.0f, S(44.0f)))) {
								server.process = std::make_unique<ServerProcess>();
								std::string error;
								const std::vector<std::string> command = ServerCommand(server.info, server.memoryMb);
								if (server.process->Start(command, fs::absolute(serverDir), fs::absolute(serverDir / "logs" / "launcher-console.log"), &error)) {
									server.running = true;
									server.seenLines = 0;
									log << "Started server " << serverPackId << "\n";
								} else {
									Toasts::Push(Toasts::Kind::Error, "Couldn't start the server", error);
									server.process.reset();
								}
							}
							ImGui::PopStyleColor(3);
							ImGui::EndDisabled();
							if (!server.ready)
								ImGui::SetItemTooltip("Create the server files first");
							else if (!server.eula)
								ImGui::SetItemTooltip("Agree to the Minecraft EULA first");
						} else {
							if (ImGui::Button("  Stop server  ", ImVec2(0.0f, S(44.0f)))) {
								server.process->Send("stop");
								server.stopRequestedAt = ImGui::GetTime();
							}
							ImGui::SameLine();
							ImGui::PushStyleColor(ImGuiCol_Button, Theme::Mix(Theme::C().cardBg, Theme::C().error, 0.55f));
							if (ImGui::Button("Force stop", ImVec2(0.0f, S(44.0f))))
								server.process->Kill();
							ImGui::PopStyleColor();
							ImGui::SetItemTooltip("Kill the server without saving");
						}
						ImGui::PopFont();
						if (server.stopRequestedAt >= 0.0 && server.running) {
							ImGui::SameLine();
							Widgets::Spinner(Theme::C().accent);
							ImGui::SameLine();
							ImGui::TextDisabled("Saving and stopping...");
						}
						// Where players join.
						ImGui::Spacing();
						// A server bound to one address only answers there, so show that one.
						const std::string& boundIp = server.props.serverIp;
						const bool loopbackOnly = boundIp.rfind("127.", 0) == 0;
						ImGui::TextDisabled("Join from this PC:");
						ImGui::SameLine();
						const std::string localAddress = (boundIp.empty() ? std::string("localhost") : boundIp) + ":" +
							std::to_string(server.props.port);
						ImGui::TextUnformatted(localAddress.c_str());
						const std::string networkIp = boundIp.empty() ? lanAddress : loopbackOnly ? std::string() : boundIp;
						if (!networkIp.empty()) {
							ImGui::TextDisabled("Friends on your network:");
							ImGui::SameLine();
							const std::string address = networkIp + ":" + std::to_string(server.props.port);
							ImGui::TextUnformatted(address.c_str());
							ImGui::SameLine();
							if (ImGui::SmallButton("Copy"))
								ImGui::SetClipboardText(address.c_str());
						}
						ImGui::TextDisabled("Friends over the internet need port %d forwarded on your router to this PC.", server.props.port);
						EndCard();

						// 5. Docker.
						ImGui::Spacing();
						BeginCard("server_docker");
						Widgets::SectionLabel("DOCKER");
						ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().textDim);
						ImGui::TextWrapped("Run this server in a container with docker-minecraft-server (itzg/minecraft-server). "
							"It uses this same folder and world, so don't also start it above.");
						ImGui::PopStyleColor();
						const bool dockerReady = dockerState == DockerState::Ready;
						const bool hostIpSet = !server.props.serverIp.empty();
						ImGui::BeginDisabled(!server.ready || server.dockerBusy || hostIpSet);
						if (ImGui::Button("Write compose file")) {
							std::string error;
							server.dockerFailed = !WriteDockerCompose(serverDir, serverPackId, server.info, server.memoryMb, &error);
							server.dockerMessage = server.dockerFailed ? error : "Wrote " + (serverDir / "docker-compose.yml").string();
						}
						ImGui::SetItemTooltip("docker-compose.yml in the server folder, for running it yourself or on another machine");
						ImGui::SameLine();
						ImGui::BeginDisabled(!dockerReady || !server.eula || server.running);
						if (ImGui::Button("Start in Docker"))
							startDockerCompose(serverPackId, { "compose", "up", "-d" },
								"Started. Watch it with \"docker compose logs -f\" in the server folder.");
						ImGui::EndDisabled();
						if (!server.eula)
							ImGui::SetItemTooltip("Agree to the Minecraft EULA first");
						else if (server.running)
							ImGui::SetItemTooltip("Stop the server above first");
						ImGui::SameLine();
						ImGui::BeginDisabled(!dockerReady);
						if (ImGui::Button("Stop in Docker"))
							startDockerCompose(serverPackId, { "compose", "down" }, "Stopped. The world was saved.");
						ImGui::EndDisabled();
						ImGui::EndDisabled();
						if (!server.ready)
							ImGui::TextDisabled("Create the server files first.");
						else if (hostIpSet)
							ImGui::TextColored(Theme::C().error, "Set Host IP to \"All network adapters\" to use Docker. Docker publishes the port itself.");
						if (dockerState == DockerState::Checking || dockerState == DockerState::Unknown) {
							Widgets::Spinner(Theme::C().accent);
							ImGui::SameLine();
							ImGui::TextDisabled("Looking for Docker...");
						} else if (dockerState == DockerState::Missing) {
							ImGui::TextDisabled("Docker isn't installed. You can still write the compose file.");
							ImGui::SameLine();
							ImGui::TextLinkOpenURL("Get Docker", "https://docs.docker.com/get-started/get-docker/");
						} else if (dockerState == DockerState::Stopped) {
							ImGui::TextColored(Theme::C().error, "Docker is installed, but its engine isn't running. Start Docker Desktop.");
							ImGui::SameLine();
							if (ImGui::SmallButton("Check again"))
								checkDocker();
						}
						if (server.dockerBusy) {
							Widgets::Spinner(Theme::C().accent);
							ImGui::SameLine();
						}
						if (!server.dockerMessage.empty()) {
							Widgets::WrappedText(server.dockerFailed ? Theme::C().error : Theme::C().textDim, server.dockerMessage.c_str());
						}
						EndCard();

						// 6. Console.
						if (server.process) {
							ImGui::Spacing();
							Widgets::SectionLabel("CONSOLE");
							std::uint64_t total = 0;
							const std::vector<std::string> lines = server.process->Lines(&total);
							ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Mix(Theme::C().windowBg, Theme::C().cardBg, 0.4f));
							ImGui::BeginChild("server_console", ImVec2(0.0f, S(300.0f)), ImGuiChildFlags_Borders,
								ImGuiWindowFlags_HorizontalScrollbar);
							const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - S(4.0f);
							ImGuiListClipper clipper;
							clipper.Begin(static_cast<int>(lines.size()));
							while (clipper.Step()) {
								for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
									const std::string& line = lines[static_cast<std::size_t>(i)];
									const bool warn = line.find("WARN") != std::string::npos;
									const bool bad = line.find("ERROR") != std::string::npos || line.find("Exception") != std::string::npos;
									if (bad || warn)
										ImGui::PushStyleColor(ImGuiCol_Text, bad ? Theme::C().error : Theme::Mix(Theme::C().error, Theme::C().text, 0.5f));
									ImGui::TextUnformatted(line.c_str());
									if (bad || warn)
										ImGui::PopStyleColor();
								}
							}
							if (total != server.seenLines && atBottom)
								ImGui::SetScrollHereY(1.0f);
							server.seenLines = total;
							ImGui::EndChild();
							ImGui::PopStyleColor();
							ImGui::BeginDisabled(!server.running);
							ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Send").x -
								ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x);
							const bool entered = ImGui::InputTextWithHint("##server_command", "Type a command, e.g. op <name>, say hi, time set day",
								server.command, sizeof(server.command), ImGuiInputTextFlags_EnterReturnsTrue);
							ImGui::SameLine();
							if ((ImGui::Button("Send") || entered) && server.command[0] != '\0') {
								std::string command = server.command;
								if (!command.empty() && command[0] == '/')
									command.erase(0, 1);
								server.process->Send(command);
								server.command[0] = '\0';
								ImGui::SetKeyboardFocusHere(-1);
							}
							ImGui::EndDisabled();
						}
					}
					ImGui::EndChild();
				}
			} else if (page == Page::Downloads) {
				std::vector<std::pair<std::string, DownloadState>> snapshot;
				{
					std::lock_guard<std::mutex> lock(downloadsMutex);
					snapshot.assign(downloads.begin(), downloads.end());
				}
				if (snapshot.empty() && Widgets::EmptyState("No downloads yet",
						"Pick a modpack on Discover and press Install.", "Browse modpacks"))
					page = Page::Discover;
				const float iconSize = S(56.0f);
				int cardIndex = 0;
				for (const auto& item : snapshot) {
					const DownloadState& state = item.second;
					Widgets::BeginEntrance(pageShownAt, cardIndex++);
					BeginCard(item.first.c_str());
					PackIconItem(state.pack, images, iconSize);
					ImGui::SameLine(0.0f, S(14.0f));
					ImGui::BeginGroup();
					ImGui::PushTextWrapPos(0.0f);
					Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::C().text,
						state.pack.name.empty() ? state.fileName.c_str() : state.pack.name.c_str());
					if (!state.pack.name.empty())
						ImGui::TextDisabled("%s", state.fileName.c_str());
					if (state.planInstall) {
						if (state.active) {
							if (state.stageTotal > 0)
								ImGui::TextDisabled("%s...  %llu of %llu", state.stage.c_str(),
									static_cast<unsigned long long>(state.stageDone), static_cast<unsigned long long>(state.stageTotal));
							else
								ImGui::TextDisabled("%s...", state.stage.c_str());
						} else if (!state.error.empty()) {
							ImGui::TextColored(Theme::C().error, "Install failed: %s", state.error.c_str());
						} else if (!state.blocked.empty()) {
							ImGui::TextColored(Theme::C().error, "Installed - %llu file(s) need a manual download (see Discover)",
								static_cast<unsigned long long>(state.blocked.size()));
						} else {
							ImGui::TextColored(Theme::C().accent, "Installed");
						}
					} else if (state.active) {
						if (state.total > 0.0)
							ImGui::TextDisabled("Downloading...  %s of %s", FormatFileSize(static_cast<std::uint64_t>(state.now)).c_str(),
								FormatFileSize(static_cast<std::uint64_t>(state.total)).c_str());
						else
							ImGui::TextDisabled("Downloading...");
					} else if (!state.error.empty()) {
						ImGui::TextColored(Theme::C().error, "Download failed: %s", state.error.c_str());
					} else if (state.installing) {
						ImGui::TextDisabled("Extracting files...");
					} else if (!state.installError.empty()) {
						ImGui::TextColored(Theme::C().error, "Extraction failed: %s", state.installError.c_str());
					} else if (state.installed) {
						ImGui::TextColored(Theme::C().accent, "Files extracted");
					} else {
						ImGui::TextDisabled("Downloaded - ready to extract.");
					}
					ImGui::EndGroup();
					ImGui::PopTextWrapPos();
					if (!state.active && !state.outPath.empty() && !state.installing && !state.installed) {
						AlignRightForButton("Install files");
						if (ImGui::Button("Install files"))
							startInstall(item.first, state.pack, state.outPath);
					}
					if (state.planInstall && state.active)
						Widgets::ThinProgressBar(state.stageTotal > 0
							? static_cast<float>(static_cast<double>(state.stageDone) / static_cast<double>(state.stageTotal)) : 0.0f);
					else if (state.active && state.total > 0.0)
						Widgets::ThinProgressBar(static_cast<float>(state.now / state.total));
					else if (state.installing)
						Widgets::ThinProgressBar(static_cast<float>(state.installProgress));
					EndCard();
					Widgets::EndEntrance();
				}
			} else {
				settingsContext.accountName = account.type == Account::Type::None ? std::string() : account.name;
				settingsContext.accountCount = static_cast<int>(accounts.accounts.size());
				settingsContext.accountKind = account.type == Account::Type::Microsoft ? "Microsoft account"
					: account.type == Account::Type::Offline ? "Offline (singleplayer)" : "No account yet";
				const std::string oldCurseForgeKey = CurseForgeApiKey();
				settingsContext.shownAt = pageShownAt;
				if (DrawSettingsPage(settings, settingsContext)) {
					settingsDirty = true;
					SetCurseForgeApiKey(settings.curseForgeApiKey);
					// A new key: refresh Discover's search on the next visit.
					if (CurseForgeApiKey() != oldCurseForgeKey && views.count(PackSource::CurseForge) > 0)
						discover.searchPending = true;
				}
			}

			// "Stop game" confirmation.
			if (!stopConfirmPackId.empty()) {
				ImGui::OpenPopup("Stop Minecraft?");
				ImGui::SetNextWindowSize(ImVec2(S(420.0f), 0.0f));
				if (ImGui::BeginPopupModal("Stop Minecraft?", nullptr, ImGuiWindowFlags_NoResize)) {
					ImGui::TextWrapped("Minecraft will be closed right away. Anything not saved since the last autosave "
						"(usually under a minute ago) will be lost.");
					ImGui::Spacing();
					ImGui::PushStyleColor(ImGuiCol_Button, Theme::Mix(Theme::C().cardBg, Theme::C().error, 0.55f));
					if (ImGui::Button("Stop game")) {
						std::lock_guard<std::mutex> lock(playsMutex);
						const auto it = plays.find(stopConfirmPackId);
						if (it != plays.end())
							it->second.stop->store(true);
						stopConfirmPackId.clear();
						ImGui::CloseCurrentPopup();
					}
					ImGui::PopStyleColor();
					ImGui::SameLine();
					if (ImGui::Button("Keep playing")) {
						stopConfirmPackId.clear();
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}

			// Per-pack launch settings.
			if (!launchSettingsPackId.empty()) {
				const auto it = libraryPacks.find(launchSettingsPackId);
				if (it == libraryPacks.end()) {
					launchSettingsPackId.clear();
				} else {
					ImGui::OpenPopup("Launch settings");
					const ImVec2 display = ImGui::GetIO().DisplaySize;
					ImGui::SetNextWindowSize(ImVec2(std::min(S(720.0f), display.x * 0.9f), std::min(S(640.0f), display.y * 0.85f)));
					if (ImGui::BeginPopupModal("Launch settings", nullptr, ImGuiWindowFlags_NoResize)) {
						Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::C().text, it->second.name.c_str());
						ImGui::TextDisabled("Used the next time you press Play for this pack.");
						const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
						ImGui::BeginChild("pack_launch_scroll", ImVec2(0.0f, -footer));
						if (DrawPackLaunchSettings(settings, launchSettingsPackId, settingsContext))
							settingsDirty = true;
						ImGui::EndChild();
						if (ImGui::Button("Done")) {
							launchSettingsPackId.clear();
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}
				}
			}

			// Optional mods picker.
			if (!picker.pack.id.empty()) {
				ImGui::OpenPopup("Optional mods");
				const ImVec2 display = ImGui::GetIO().DisplaySize;
				ImGui::SetNextWindowSize(ImVec2(std::min(S(640.0f), display.x * 0.9f), std::min(S(600.0f), display.y * 0.85f)));
				if (ImGui::BeginPopupModal("Optional mods", nullptr, ImGuiWindowFlags_NoResize)) {
					Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::C().text, picker.pack.name.c_str());
					bool close = false;
					if (picker.loading) {
						Widgets::Spinner(Theme::C().accent);
						ImGui::SameLine();
						ImGui::TextDisabled("Loading optional mods...");
					} else if (!picker.error.empty()) {
						ImGui::TextColored(Theme::C().error, "Couldn't load optional mods: %s", picker.error.c_str());
					} else {
						ImGui::TextDisabled("Choose extra mods to install. Mods they need are added too.");
					}
					const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
					ImGui::BeginChild("optional_list", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders);
					for (std::size_t i = 0; i < picker.mods.size(); ++i) {
						const OptionalMod& mod = picker.mods[i];
						ImGui::PushID(static_cast<int>(i));
						bool on = picker.chosen[i] != 0;
						if (ImGui::Checkbox(mod.name.c_str(), &on))
							setOptional(i, on);
						if (!mod.group.empty()) {
							ImGui::SameLine();
							ImGui::TextDisabled("(pick one: %s)", mod.group.c_str());
						}
						if (!mod.description.empty()) {
							ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
							ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().textDim);
							ImGui::TextWrapped("%s", mod.description.c_str());
							ImGui::PopStyleColor();
							ImGui::Unindent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
						}
						ImGui::PopID();
					}
					ImGui::EndChild();
					ImGui::BeginDisabled(picker.loading);
					if (ImGui::Button(picker.error.empty() ? "Install" : "Install without optional mods")) {
						Modpack pack = picker.pack;
						pack.optionalMods.clear();
						for (std::size_t i = 0; i < picker.mods.size(); ++i) {
							if (picker.chosen[i])
								pack.optionalMods.push_back(picker.mods[i].name);
						}
						startPlanInstall(pack);
						close = true;
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					if (ImGui::Button("Cancel##optional"))
						close = true;
					if (close) {
						picker.pack = Modpack{};
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}

			// "Delete server" confirmation.
			if (!deleteServerPackId.empty()) {
				ImGui::OpenPopup("Delete server?");
				ImGui::SetNextWindowSize(ImVec2(S(460.0f), 0.0f));
				if (ImGui::BeginPopupModal("Delete server?", nullptr, ImGuiWindowFlags_NoResize)) {
					const auto libraryPack = libraryPacks.find(deleteServerPackId);
					const std::string name = libraryPack != libraryPacks.end() ? libraryPack->second.name : deleteServerPackId;
					ImGui::TextWrapped("%s's server will be deleted: its world, configuration, whitelist and logs. "
						"The pack stays in My Library. This can't be undone.", name.c_str());
					ImGui::TextDisabled("%s", (fs::path(kServersRoot) / deleteServerPackId).string().c_str());
					ImGui::Spacing();
					bool blocked = false;
					{
						std::lock_guard<std::mutex> lock(serversMutex);
						const auto it = servers.find(deleteServerPackId);
						blocked = it != servers.end() && (it->second.running || it->second.busy);
					}
					if (blocked)
						ImGui::TextColored(Theme::C().error, "Stop the server and wait for setup to finish first.");
					ImGui::PushStyleColor(ImGuiCol_Button, Theme::Mix(Theme::C().cardBg, Theme::C().error, 0.55f));
					ImGui::BeginDisabled(blocked);
					if (ImGui::Button("Delete server")) {
						startServerDelete(deleteServerPackId, name);
						deleteServerPackId.clear();
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();
					ImGui::PopStyleColor();
					ImGui::SameLine();
					if (ImGui::Button("Keep it##server")) {
						deleteServerPackId.clear();
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}

			// "Uninstall" confirmation.
			if (!uninstallPackId.empty()) {
				const auto it = libraryPacks.find(uninstallPackId);
				if (it == libraryPacks.end()) {
					uninstallPackId.clear();
				} else {
					ImGui::OpenPopup("Uninstall pack?");
					ImGui::SetNextWindowSize(ImVec2(S(460.0f), 0.0f));
					if (ImGui::BeginPopupModal("Uninstall pack?", nullptr, ImGuiWindowFlags_NoResize)) {
						const Modpack pack = it->second;
						const InstalledVersion* one = uninstallVersionKey.empty() ? nullptr
							: FindInstalledVersion(pack, uninstallVersionKey);
						if (one) {
							ImGui::TextWrapped("Version %s of %s will be removed and its files deleted, including worlds in "
								"its saves folder. Other versions stay. This can't be undone.", one->version.c_str(), pack.name.c_str());
							ImGui::TextDisabled("%s", one->gameDir.c_str());
						} else {
							ImGui::TextWrapped("%s will be removed from My Library and the files of %s deleted, including "
								"worlds in their saves folders. This can't be undone.", pack.name.c_str(),
								pack.installedVersions.size() > 1 ? "all its versions" : "the pack");
							for (const InstalledVersion& v : pack.installedVersions)
								ImGui::TextDisabled("%s", v.gameDir.c_str());
						}
						ImGui::Spacing();
						bool busy = packUpdates.count(pack.id) > 0;
						{
							std::lock_guard<std::mutex> lock(playsMutex);
							const auto play = plays.find(pack.id);
							busy = busy || (play != plays.end() && play->second.busy);
						}
						{
							std::lock_guard<std::mutex> lock(downloadsMutex);
							for (const auto& [key, state] : downloads)
								busy = busy || (state.pack.id == pack.id && (state.active || state.installing));
						}
						const fs::path packServerDir = fs::path(kServersRoot) / pack.id;
						std::error_code serverDirError;
						// One server per pack: offered only when the whole pack goes.
						const bool removesPack = !one || pack.installedVersions.size() <= 1;
						if (removesPack && fs::is_directory(packServerDir, serverDirError)) {
							ImGui::Checkbox("Also delete its server", &uninstallDeleteServer);
							ImGui::SetItemTooltip("Deletes %s, including the server's world. Otherwise the server is kept "
								"and still listed on the Servers page.", packServerDir.string().c_str());
							if (uninstallDeleteServer) {
								std::lock_guard<std::mutex> lock(serversMutex);
								const auto server = servers.find(pack.id);
								busy = busy || (server != servers.end() && (server->second.running || server->second.busy));
							}
						} else {
							uninstallDeleteServer = false;
						}
						if (busy)
							ImGui::TextColored(Theme::C().error, uninstallDeleteServer
								? "Close the game, stop the server, and wait for downloads, installs and server setup to finish first."
								: "Close the game and wait for downloads or installs of this pack to finish first.");
						ImGui::PushStyleColor(ImGuiCol_Button, Theme::Mix(Theme::C().cardBg, Theme::C().error, 0.55f));
						ImGui::BeginDisabled(busy);
						if (ImGui::Button("Uninstall")) {
							const bool lastVersion = !one || pack.installedVersions.size() <= 1;
							if (lastVersion && settings.packLaunch.erase(pack.id) > 0)
								settingsDirty = true;
							uninstallPack(pack, one ? uninstallVersionKey : std::string());
							// The server goes only with the pack's last version.
							if (uninstallDeleteServer && lastVersion)
								startServerDelete(pack.id, pack.name);
							uninstallPackId.clear();
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndDisabled();
						ImGui::PopStyleColor();
						ImGui::SameLine();
						if (ImGui::Button("Keep it")) {
							uninstallPackId.clear();
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}
				}
			}

			if (!updateCheck.packId.empty()) {
				const auto it = libraryPacks.find(updateCheck.packId);
				if (it == libraryPacks.end()) {
					updateCheck = UpdateCheck{};
				} else {
					ImGui::OpenPopup("Update pack");
					ImGui::SetNextWindowSize(ImVec2(S(460.0f), 0.0f));
					if (ImGui::BeginPopupModal("Update pack", nullptr, ImGuiWindowFlags_NoResize)) {
						const Modpack pack = it->second;
						const Modpack target = updateCheck.target;
						const InstalledVersion* installed = target.id.empty() ? nullptr
							: FindInstalledVersion(pack, VersionKey(target));
						bool canUpdate = false;
						if (updateCheck.loading) {
							Widgets::Spinner(Theme::C().accent);
							ImGui::SameLine();
							ImGui::TextDisabled("Checking %s for the newest version...", PackSourceName(pack.source));
						} else if (!updateCheck.error.empty()) {
							ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().error);
							ImGui::TextWrapped("Couldn't check for updates: %s", updateCheck.error.c_str());
							ImGui::PopStyleColor();
						} else if (installed && installed->key == pack.activeVersion) {
							ImGui::TextWrapped("%s %s is the newest version.", pack.name.c_str(), pack.version.c_str());
						} else if (installed) {
							ImGui::TextWrapped("The newest version, %s, is already installed. Pick it in the card's version "
								"dropdown to play it.", target.version.c_str());
						} else {
							canUpdate = true;
							std::string newest = target.version;
							if (!target.gameVersion.empty())
								newest += " (Minecraft " + target.gameVersion + ")";
							ImGui::TextWrapped("Update %s from %s to %s?", pack.name.c_str(), pack.version.c_str(), newest.c_str());
							ImGui::Spacing();
							ImGui::TextWrapped("The new version is installed next to the current one. Your worlds, screenshots, "
								"resource and shader packs, schematics, server list and game options are copied over. "
								"Files the new version ships itself take their place.");
							if (!target.gameVersion.empty() && !pack.gameVersion.empty() && target.gameVersion != pack.gameVersion) {
								ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().error);
								ImGui::TextWrapped("The Minecraft version changes from %s to %s. Back up worlds you care about.",
									pack.gameVersion.c_str(), target.gameVersion.c_str());
								ImGui::PopStyleColor();
							}
							if (!pack.optionalMods.empty())
								ImGui::TextDisabled("Your optional mod choices are kept.");
							ImGui::Spacing();
							ImGui::Checkbox(("Remove " + pack.version + " afterwards").c_str(), &updateCheck.removeOld);
							ImGui::SetItemTooltip("Deletes the old version's folders once everything has been copied, "
								"including its saves folder. Leave it off to keep the old version installed.");
						}
						bool busy = packUpdates.count(pack.id) > 0;
						{
							std::lock_guard<std::mutex> lock(playsMutex);
							const auto play = plays.find(pack.id);
							busy = busy || (play != plays.end() && (play->second.busy || play->second.playing));
						}
						{
							std::lock_guard<std::mutex> lock(downloadsMutex);
							for (const auto& [key, state] : downloads)
								busy = busy || (state.pack.id == pack.id && (state.active || state.installing));
						}
						if (canUpdate && busy)
							ImGui::TextColored(Theme::C().error, "Close the game and wait for downloads or installs of this pack to finish first.");
						ImGui::Spacing();
						if (canUpdate) {
							ImGui::BeginDisabled(busy);
							if (ImGui::Button("Update")) {
								startUpdate(pack, target, updateCheck.removeOld);
								updateCheck = UpdateCheck{};
								ImGui::CloseCurrentPopup();
							}
							ImGui::EndDisabled();
							ImGui::SameLine();
						} else if (!updateCheck.loading && !updateCheck.error.empty()) {
							if (ImGui::Button("Try again"))
								startUpdateCheck(pack);
							ImGui::SameLine();
						}
						if (ImGui::Button(canUpdate ? "Not now" : "Close")) {
							updateCheck = UpdateCheck{};
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}
				}
			}

			Widgets::EndEntrance();
			Widgets::SmoothScroll();
			ImGui::EndChild();
			ImGui::End();
			Toasts::Draw(settings.reduceMotion);

			ImGui::Render();
			int display_w = 0;
			int display_h = 0;
			glfwGetFramebufferSize(window, &display_w, &display_h);
			glViewport(0, 0, display_w, display_h);
			const ImVec4& clear = Theme::C().windowBg;
			glClearColor(clear.x, clear.y, clear.z, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			glfwSwapBuffers(window);
		}

		// Running servers get "stop" (they save their worlds), then a few seconds before being killed.
		for (auto& [packId, server] : servers) {
			if (server.process && server.process->Running()) {
				log << "Stopping server " << packId << "\n";
				server.process->Send("stop");
			}
		}
		for (auto& [packId, server] : servers) {
			if (server.process && !server.process->WaitForExit(20000))
				server.process->Kill();
			std::lock_guard<std::mutex> lock(serversMutex);
			server.cancel->store(true);
		}

		// Ask in-flight downloads/extractions to stop, then wait for them.
		shuttingDown.store(true);
		signInCancel->store(true);
		{
			// Stops preparing packs. A running Minecraft keeps running.
			std::lock_guard<std::mutex> lock(playsMutex);
			for (auto& [packId, play] : plays)
				play.cancel->store(true);
		}
		for (Worker& worker : workers) {
			if (worker.thread.joinable())
				worker.thread.join();
		}
		uiQueue.Drain(); // saves downloads/extractions that finished during shutdown

		// Remember the window for next time (and any unsaved settings).
		lastPlacement.saved = lastPlacement.width >= 200 && lastPlacement.height >= 150;
		settings.window = lastPlacement;
		if (!SaveSettings(kSettingsFile, settings))
			log << "Could not save " << kSettingsFile << "\n";
		images.Shutdown(); // frees textures while the GL context still exists
		mascot.Shutdown();
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		glfwDestroyWindow(window);
		glfwTerminate();
		log << "Exiting normally\n";
		return 0;
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

#include "imgui.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "Model/Modpack.h"
#include "Platform.h"
#include "Network/HttpClient.h"
#include "Network/Modrinth.h"
#include "Storage/Installer.h"
#include "Storage/Settings.h"
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
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
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
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#pragma comment(lib, "dwmapi.lib")
#endif

namespace fs = std::filesystem;
using Theme::S;

static const fs::path kIndexRelativePath = fs::path("resources") / "index.json";
static const char* kAppVersion = "0.1.0";
static const char* kSettingsFile = "settings.json";
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
};

enum class Page {
	Discover,
	Library,
	Downloads,
	Settings
};

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

static bool IsPackInstalled(const Modpack& pack, const std::map<std::string, DownloadState>& downloads)
{
	for (const PackFile& file : pack.files) {
		const auto state = downloads.find(file.url);
		if (state != downloads.end() && state->second.installed)
			return true;
	}
	return false;
}

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
	ImGui::SameLine(std::max(ImGui::GetCursorPosX(), x));
}

// Starts a card: a bordered child with the card background color that grows to fit
// its contents. Pair with EndCard().
static void BeginCard(const char* id)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::C().cardBg);
	ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
}

static void EndCard()
{
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

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

		const char* glsl_version = "#version 130";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		// Size the window in logical pixels, so it isn't tiny on high-DPI (e.g. 4K at 150%) monitors.
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

		GLFWwindow* window = glfwCreateWindow(1360, 840, "MC Launcher Premium", nullptr, nullptr);
		if (window == nullptr) {
			log << "glfwCreateWindow failed\n";
			glfwTerminate();
			return -1;
		}
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
		Settings settings = LoadSettings(kSettingsFile);
		bool settingsDirty = false; // saved once no widget is being edited
		float dpiScale = 1.0f;
		glfwGetWindowContentScale(window, &dpiScale, nullptr);
		float appliedUiScale = settings.uiScale;
		std::size_t appliedTheme = Theme::FindTheme(settings.theme);
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
		std::vector<Modpack> packs;
		int selected = -1;
		Page page = Page::Discover;
		char search[128] = "";
		bool focusSearch = false; // Ctrl+F
		HttpClient http;
		ImageCache images(http);
		std::map<std::string, DownloadState> downloads;
		std::mutex downloadsMutex;
		std::vector<Worker> workers;
		std::atomic<bool> shuttingDown{ false };
		UiQueue uiQueue;
		fs::create_directories("downloads");
		fs::create_directories("installs");

		// Search state (UI thread only). Typing schedules a search; it runs once
		// the query has been unchanged for kSearchDelaySeconds.
		bool searchPending = true; // run the initial "most downloaded" search on the first frame
		double lastSearchEdit = -1.0;
		bool searching = false;
		int searchGeneration = 0; // results from older searches are ignored
		std::string searchStatus;
		bool searchFailed = false;
		// Per-pack version lookups for Modrinth results, keyed by pack id.
		std::set<std::string> filesLoading;
		std::map<std::string, std::string> filesErrors;
		// Full-size banner lookups, keyed by pack id.
		std::set<std::string> bannersLoading;

		std::error_code cwdError;
		SettingsPageContext settingsContext;
		settingsContext.appVersion = kAppVersion;
		settingsContext.dataFolder = fs::current_path(cwdError).string();
		settingsContext.systemMemoryMb = Platform::SystemMemoryMb();
		settingsContext.dpiScale = dpiScale;
		// Packs the user has started installing, so My Library survives new searches.
		std::map<std::string, Modpack> libraryPacks;

		auto startSearch = [&]() {
			const int generation = ++searchGeneration;
			const std::string query = search;
			const int limit = settings.searchLimit;
			searching = true;
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &packs, &selected, &searching,
				&searchStatus, &searchFailed, &searchGeneration, &localPacks, &log, query, limit, generation]() {
				ModrinthSearchResult result;
				std::string error;
				const bool ok = SearchModrinthModpacks(http, query, limit, result, &error, &shuttingDown);
				uiQueue.Post([&packs, &selected, &searching, &searchStatus, &searchFailed, &searchGeneration,
					&localPacks, &log, query, generation, ok, result, error]() {
					if (generation != searchGeneration)
						return;
					searching = false;
					searchFailed = !ok;
					if (ok) {
						packs = result.packs;
						searchStatus = FormatCount(result.totalHits) + " modpacks on Modrinth";
						log << "Search \"" << query << "\": " << result.packs.size() << " of "
							<< result.totalHits << " result(s)\n";
					} else {
						log << "Search \"" << query << "\" failed: " << error << "\n";
						// Offline or API error: fall back to the local index.
						const std::string lowerQuery = ToLower(query);
						packs.clear();
						for (const Modpack& pack : localPacks) {
							if (lowerQuery.empty() || ContainsIgnoreCase(pack.name, lowerQuery) ||
								ContainsIgnoreCase(pack.description, lowerQuery))
								packs.push_back(pack);
						}
						searchStatus = "Modrinth unavailable (" + error + ") - showing local packs";
					}
					selected = packs.empty() ? -1 : 0;
				});
			});
		};

		auto startFetchFiles = [&](const std::string& packId) {
			filesLoading.insert(packId);
			filesErrors.erase(packId);
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &packs, &filesLoading, &filesErrors, &log, packId]() {
				ModrinthVersion version;
				std::string error;
				const bool ok = FetchLatestModrinthVersion(http, packId, version, &error, &shuttingDown);
				uiQueue.Post([&packs, &filesLoading, &filesErrors, &log, packId, ok, version, error]() {
					filesLoading.erase(packId);
					if (!ok) {
						log << "Version lookup for " << packId << " failed: " << error << "\n";
						filesErrors[packId] = error;
						return;
					}
					log << "Version lookup for " << packId << ": " << version.versionNumber << " ("
						<< (version.files.empty() ? std::string("no file") : version.files.front().name) << ")\n";
					for (Modpack& pack : packs) {
						if (pack.id != packId)
							continue;
						pack.version = version.versionNumber;
						if (!version.gameVersion.empty())
							pack.gameVersion = version.gameVersion;
						pack.loaders = version.loaders;
						pack.files = version.files;
						pack.filesLoaded = true;
					}
				});
			});
		};

		auto startFetchBanner = [&](const std::string& packId) {
			bannersLoading.insert(packId);
			StartWorker(workers, [&http, &uiQueue, &shuttingDown, &packs, &bannersLoading, &log, packId]() {
				std::string url;
				std::string error;
				const bool ok = FetchModrinthBanner(http, packId, url, &error, &shuttingDown);
				uiQueue.Post([&packs, &bannersLoading, &log, packId, ok, url, error]() {
					bannersLoading.erase(packId);
					if (!ok)
						log << "Banner lookup for " << packId << " failed: " << error << "\n";
					for (Modpack& pack : packs) {
						if (pack.id != packId)
							continue;
						pack.bannerHiResUrl = url;
						pack.bannerLoaded = true; // also on failure: keep the thumbnail, don't retry
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
			const std::size_t wantedTheme = Theme::FindTheme(settings.theme);
			if (contentScale > 0.0f && (contentScale != dpiScale || settings.uiScale != appliedUiScale || wantedTheme != appliedTheme)) {
				const bool themeChanged = wantedTheme != appliedTheme;
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
			ImGui::BeginChild("sidebar", ImVec2(S(230.0f), 0.0f), ImGuiChildFlags_Borders);
			{
				// Logo: a little grass block, then the name.
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				const ImVec2 logo = ImGui::GetCursorScreenPos();
				const float block = S(34.0f);
				const float px = block / 8.0f;
				for (int y = 0; y < 8; ++y) {
					for (int x = 0; x < 8; ++x) {
						const bool grass = y < 3 || (y == 3 && (x * 7 + 3) % 3 == 0);
						const float shade = 0.85f + 0.15f * static_cast<float>((x * 5 + y * 3) % 4) / 3.0f;
						const ImVec4 color = grass ? ImVec4(0.30f * shade, 0.68f * shade, 0.36f * shade, 1.0f)
							: ImVec4(0.47f * shade, 0.33f * shade, 0.22f * shade, 1.0f);
						drawList->AddRectFilled(ImVec2(logo.x + x * px, logo.y + y * px),
							ImVec2(logo.x + (x + 1) * px, logo.y + (y + 1) * px), ImGui::GetColorU32(color));
					}
				}
				ImGui::Dummy(ImVec2(block, block));
				ImGui::SameLine(0.0f, S(12.0f));
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
			ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
			Widgets::SectionLabel("LAUNCHER");
			if (Widgets::NavItem("Settings", page == Page::Settings))
				page = Page::Settings;

			// Footer: UI scale control.
			const float footerHeight = ImGui::GetFrameHeight() + ImGui::GetTextLineHeightWithSpacing() * 2.0f +
				ImGui::GetStyle().ItemSpacing.y * 3.0f;
			ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - footerHeight - ImGui::GetStyle().WindowPadding.y));
			ImGui::Separator();
			Widgets::SectionLabel("UI SCALE");
			if (ImGui::Button("-", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
				setUiScale(settings.uiScale - kUiScaleStep);
			ImGui::SetItemTooltip("Smaller (Ctrl -)");
			ImGui::SameLine();
			char scaleLabel[32];
			std::snprintf(scaleLabel, sizeof(scaleLabel), "%.0f%%##scale", settings.uiScale * 100.0f);
			const float plusWidth = ImGui::GetFrameHeight();
			if (ImGui::Button(scaleLabel, ImVec2(ImGui::GetContentRegionAvail().x - plusWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f)))
				setUiScale(1.0f);
			ImGui::SetItemTooltip("Reset to 100%% (Ctrl 0)");
			ImGui::SameLine();
			if (ImGui::Button("+", ImVec2(plusWidth, 0.0f)))
				setUiScale(settings.uiScale + kUiScaleStep);
			ImGui::SetItemTooltip("Larger (Ctrl +)");
			ImGui::TextDisabled("Launching: coming soon");
			ImGui::EndChild();

			// ---- Main content ----
			ImGui::SameLine();
			ImGui::BeginChild("main_content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
			const char* breadcrumb = page == Page::Discover ? "MINECRAFT  /  MODPACKS"
				: page == Page::Library ? "MINECRAFT  /  LIBRARY"
				: page == Page::Downloads ? "MINECRAFT  /  DOWNLOADS" : "LAUNCHER  /  SETTINGS";
			Widgets::Text(fonts.bold, Theme::kFontSmall, Theme::C().accent, breadcrumb);
			if (page == Page::Discover) {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::C().text, "Discover modpacks");
				ImGui::TextDisabled("Find a pack, download its files, and prepare it for Minecraft.");
			} else if (page == Page::Library) {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::C().text, "My library");
				ImGui::TextDisabled("Modpack files you have extracted are listed here.");
			} else if (page == Page::Downloads) {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::C().text, "Downloads");
				ImGui::TextDisabled("Track downloads and extraction progress.");
			} else {
				Widgets::Text(fonts.bold, Theme::kFontTitle, Theme::C().text, "Settings");
				ImGui::TextDisabled("Themes, Minecraft launch defaults, and more.");
			}
			ImGui::Spacing();

			if (page == Page::Discover) {
				ImGui::SetNextItemWidth(-1.0f);
				if (focusSearch) {
					ImGui::SetKeyboardFocusHere();
					focusSearch = false;
				}
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14.0f), S(11.0f)));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
				if (ImGui::InputTextWithHint("##search", "Search Modrinth modpacks...", search, sizeof(search))) {
					searchPending = true;
					lastSearchEdit = ImGui::GetTime();
				}
				ImGui::PopStyleVar(2);
				if (searchPending && ImGui::GetTime() - lastSearchEdit >= kSearchDelaySeconds) {
					searchPending = false;
					startSearch();
				}
				if (searching) {
					Widgets::Spinner(Theme::C().accent);
					ImGui::SameLine();
					ImGui::TextDisabled("Searching Modrinth...");
				} else if (searchFailed) {
					ImGui::TextColored(Theme::C().error, "%s", searchStatus.c_str());
				} else {
					ImGui::TextDisabled("%s", searchStatus.c_str());
				}

				// Pack list
				ImGui::BeginChild("pack_list", ImVec2(S(340.0f), 0.0f), ImGuiChildFlags_Borders);
				Widgets::SectionLabel(search[0] == '\0' ? "MOST DOWNLOADED" : "RESULTS");
				for (std::size_t i = 0; i < packs.size(); ++i) {
					const Modpack& item = packs[i];
					std::string meta;
					if (item.fromModrinth)
						meta = item.gameVersion + "  \xC2\xB7  " + FormatCount(item.downloads) + " downloads";
					else
						meta = item.version + "  \xC2\xB7  " + std::to_string(item.files.size()) + " files";
					ImGui::PushID(static_cast<int>(i));
					if (Widgets::PackRow(item, images, selected == static_cast<int>(i), meta))
						selected = static_cast<int>(i);
					ImGui::PopID();
				}
				if (packs.empty() && !searching)
					ImGui::TextDisabled("No modpacks found.");
				ImGui::EndChild();
				ImGui::SameLine();

				// Pack details
				ImGui::BeginChild("pack_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
				if (selected >= 0 && selected < static_cast<int>(packs.size())) {
					const Modpack& pack = packs[static_cast<std::size_t>(selected)];
					const bool loadingFiles = filesLoading.count(pack.id) > 0;
					const auto filesError = filesErrors.find(pack.id);
					if (pack.fromModrinth && !pack.filesLoaded && !loadingFiles && filesError == filesErrors.end())
						startFetchFiles(pack.id);
					if (settings.showGalleryBanners && pack.fromModrinth && !pack.bannerLoaded && bannersLoading.count(pack.id) == 0)
						startFetchBanner(pack.id);

					Widgets::HeroBanner(pack, images, std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, S(200.0f), S(340.0f)),
						settings.showGalleryBanners);
					ImGui::Spacing();

					if (pack.fromModrinth) {
						Widgets::Chip((FormatCount(pack.downloads) + " downloads").c_str(), Theme::C().accent);
						ImGui::SameLine();
						ImGui::AlignTextToFramePadding();
						ImGui::TextDisabled("modrinth.com/modpack/%s", pack.id.c_str());
					}
					ImGui::Spacing();
					ImGui::PushFont(nullptr, Theme::kFontBody + 1.0f);
					ImGui::TextWrapped("%s", pack.description.c_str());
					ImGui::PopFont();
					ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

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
							startFetchFiles(pack.id);
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
							const char* label = hasState ? "Retry" : "Download";
							AlignRightForButton(label);
							if (ImGui::Button(label)) {
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
								StartWorker(workers, [&http, &downloads, &downloadsMutex, &shuttingDown, &uiQueue, url, packDownloadDir, outputPath,
									packName = pack.name]() {
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
									std::lock_guard<std::mutex> lock(downloadsMutex);
									DownloadState& current = downloads[url];
									current.active = false;
									if (ok)
										current.outPath = outputPath;
									else
										current.error = error;
									if (!shuttingDown.load()) {
										uiQueue.Post([ok, packName, error]() {
											if (ok)
												Toasts::Push(Toasts::Kind::Success, "Download complete", packName + " is ready to install.");
											else
												Toasts::Push(Toasts::Kind::Error, "Download failed", packName + ": " + error);
										});
									}
								});
							}
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
							AlignRightForButton("Extracted");
							ImGui::AlignTextToFramePadding();
							ImGui::TextColored(Theme::C().accent, "Extracted");
						} else if (!state.outPath.empty()) {
							AlignRightForButton("Install files");
							if (ImGui::Button("Install files")) {
								libraryPacks[pack.id] = pack;
								{
									std::lock_guard<std::mutex> lock(downloadsMutex);
									DownloadState& current = downloads[file.url];
									current.installing = true;
									current.installProgress = 0.0;
									current.installError.clear();
								}
								const std::string zipPath = state.outPath;
								const std::string targetDir = (fs::path("installs") / pack.id).string();
								const std::string url = file.url;
								StartWorker(workers, [zipPath, targetDir, &downloads, &downloadsMutex, &shuttingDown, &uiQueue, url,
									packName = pack.name]() {
									std::string error;
									auto progress = [&](int total, int now) {
										std::lock_guard<std::mutex> lock(downloadsMutex);
										DownloadState& current = downloads[url];
										if (total > 0)
											current.installProgress = static_cast<double>(now) / static_cast<double>(total);
									};
									const bool ok = InstallZip(zipPath, targetDir, progress, &error, &shuttingDown);
									std::lock_guard<std::mutex> lock(downloadsMutex);
									DownloadState& current = downloads[url];
									current.installing = false;
									if (ok)
										current.installed = true;
									else
										current.installError = error;
									if (!shuttingDown.load()) {
										uiQueue.Post([ok, packName, error]() {
											if (ok)
												Toasts::Push(Toasts::Kind::Success, "Files extracted", packName + " is in your library.");
											else
												Toasts::Push(Toasts::Kind::Error, "Extraction failed", packName + ": " + error);
										});
									}
								});
							}
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

					ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
					ImGui::PushFont(fonts.bold, Theme::kFontHeading);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
					ImGui::BeginDisabled(true);
					ImGui::Button("PLAY  \xE2\x80\x94  COMING SOON", ImVec2(-1.0f, S(52.0f)));
					ImGui::EndDisabled();
					ImGui::PopStyleVar();
					ImGui::PopFont();
					ImGui::TextDisabled("Minecraft installation, account sign-in, and launching are not wired up yet.");
				} else {
					ImGui::TextDisabled("No modpacks match your search.");
				}
				ImGui::EndChild();
			} else if (page == Page::Library) {
				std::lock_guard<std::mutex> lock(downloadsMutex);
				bool anyInstalled = false;
				const float iconSize = S(64.0f);
				for (const auto& [packId, pack] : libraryPacks) {
					if (!IsPackInstalled(pack, downloads))
						continue;
					anyInstalled = true;
					BeginCard(pack.id.c_str());
					PackIconItem(pack, images, iconSize);
					ImGui::SameLine(0.0f, S(14.0f));
					ImGui::BeginGroup();
					Widgets::Text(fonts.bold, Theme::kFontHeading, Theme::C().text, pack.name.c_str());
					ImGui::TextDisabled("%s  \xC2\xB7  %llu extracted file(s)",
						pack.version.c_str(), static_cast<unsigned long long>(pack.files.size()));
					ImGui::TextDisabled("Files are prepared; this prototype does not launch Minecraft yet.");
					ImGui::EndGroup();
					AlignRightForButton("Open folder");
					if (ImGui::Button("Open folder"))
						Platform::OpenFolder((fs::path("installs") / pack.id).string());
					EndCard();
				}
				if (!anyInstalled && Widgets::EmptyState("Your library is empty",
						"Packs you download and install from Discover show up here.", "Browse modpacks"))
					page = Page::Discover;
			} else if (page == Page::Downloads) {
				std::vector<std::pair<std::string, DownloadState>> snapshot;
				{
					std::lock_guard<std::mutex> lock(downloadsMutex);
					snapshot.assign(downloads.begin(), downloads.end());
				}
				if (snapshot.empty() && Widgets::EmptyState("No downloads yet",
						"Pick a modpack on Discover and press Download.", "Browse modpacks"))
					page = Page::Discover;
				const float iconSize = S(56.0f);
				for (const auto& item : snapshot) {
					const DownloadState& state = item.second;
					BeginCard(item.first.c_str());
					PackIconItem(state.pack, images, iconSize);
					ImGui::SameLine(0.0f, S(14.0f));
					ImGui::BeginGroup();
					Widgets::Text(fonts.bold, Theme::kFontBody, Theme::C().text,
						state.pack.name.empty() ? state.fileName.c_str() : state.pack.name.c_str());
					if (!state.pack.name.empty())
						ImGui::TextDisabled("%s", state.fileName.c_str());
					if (state.active) {
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
						ImGui::TextDisabled("Downloaded - open Discover to extract this pack.");
					}
					ImGui::EndGroup();
					if (state.active && state.total > 0.0)
						Widgets::ThinProgressBar(static_cast<float>(state.now / state.total));
					else if (state.installing)
						Widgets::ThinProgressBar(static_cast<float>(state.installProgress));
					EndCard();
				}
			} else {
				if (DrawSettingsPage(settings, settingsContext))
					settingsDirty = true;
			}

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

		// Ask in-flight downloads/extractions to stop, then wait for them.
		shuttingDown.store(true);
		for (Worker& worker : workers) {
			if (worker.thread.joinable())
				worker.thread.join();
		}

		if (settingsDirty)
			SaveSettings(kSettingsFile, settings);
		images.Shutdown(); // frees textures while the GL context still exists
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

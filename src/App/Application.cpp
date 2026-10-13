#include "App/Application.h"

#include "App/AppRoot.h"
#include "App/GlfwWindow.h"
#include "App/FramePacing.h"
#include "App/ThemeController.h"
#include "Platform/Platform.h"
#include "Services/AppPaths.h"
#include "Services/Settings/SettingsStore.h"
#include "UI/Theme.h"
#include "Util/FramePerf.h"
#include "imgui.h"

#include <algorithm>
#include <chrono>

int Application::Run()
{
	SettingsStore settings(AppPaths::kSettingsFile, m_log);
	std::unique_ptr<GlfwWindow> window = GlfwWindow::Create(settings.Get().window, m_log);
	if (!window)
		return -1;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// UI scale = monitor DPI scale (from the OS display settings) x the user's UI scale setting.
	ThemeController theme(*window, settings);
	theme.Initialize();
	m_log << "DPI scale " << theme.DpiScale() << ", UI scale " << settings.Get().uiScale << ", theme "
		<< Theme::Themes()[Theme::FindTheme(settings.Get().theme)].id << "\n";

	if (!window->InitImGuiBackends(m_log)) {
		ImGui::DestroyContext();
		return -1;
	}
	{
		AppRoot root(m_dataRoot, m_log, *window, settings, theme, m_offline);
		// Sleep between frames instead of letting vsync pace the loop (see App/FramePacing.h).
		using Clock = std::chrono::steady_clock;
		Platform::UsePreciseTimer();
		Clock::time_point nextFrame = Clock::now();
		Clock::time_point lastActivity = nextFrame;
		FramePacing::Pace pace;
		while (!window->ShouldClose()) {
			{
				FramePerf::Scope scope(FramePerf::kPoll);
				if (pace.interval <= 0.0) {
					window->PollEvents();
				} else {
					for (Clock::time_point now = Clock::now(); now < nextFrame; now = Clock::now()) {
						window->WaitEvents(std::chrono::duration<double>(nextFrame - now).count());
						// Slowed down for lack of activity: the first event ends the wait, so the UI answers at once.
						if (pace.idle && Clock::now() < nextFrame)
							break;
					}
				}
			}
			const Clock::time_point frameStart = Clock::now();
			if (root.Frame(window->TakeDroppedPaths()))
				lastActivity = frameStart;
			FramePerf::FrameDone();

			const Settings& current = root.CurrentSettings();
			FramePacing::Input input;
			input.maxFps = current.maxFps;
			input.lowerWhenIdle = current.lowerIdleFrameRate;
			input.focused = window->Focused();
			input.minimized = window->Minimized();
			input.idleSeconds = std::chrono::duration<double>(frameStart - lastActivity).count();
			pace = FramePacing::Choose(input);
			nextFrame = std::max(Clock::now(), frameStart + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(pace.interval)));
		}
		root.Shutdown();
	}
	window->ShutdownImGuiBackends();
	ImGui::DestroyContext();
	window.reset();
	m_log << "Exiting normally\n";
	return 0;
}

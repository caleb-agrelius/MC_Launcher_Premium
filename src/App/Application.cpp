#include "App/Application.h"

#include "App/AppRoot.h"
#include "App/GlfwWindow.h"
#include "App/ThemeController.h"
#include "Services/AppPaths.h"
#include "Services/Settings/SettingsStore.h"
#include "UI/Theme.h"
#include "imgui.h"

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
		AppRoot root(m_dataRoot, m_log, *window, settings, theme);
		while (!window->ShouldClose()) {
			window->PollEvents();
			root.Frame(window->TakeDroppedPaths());
		}
		root.Shutdown();
	}
	window->ShutdownImGuiBackends();
	ImGui::DestroyContext();
	window.reset();
	m_log << "Exiting normally\n";
	return 0;
}

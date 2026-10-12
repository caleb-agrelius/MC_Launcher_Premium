#include "Toasts.h"

#include "Theme.h"
#include "Widgets.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace Toasts {

using Theme::S;

static constexpr double kLifetime = 4.5;  // seconds on screen
static constexpr double kFadeTime = 0.25; // fade/slide in and out
static constexpr std::size_t kMaxVisible = 4;

struct Toast {
	int id = 0;
	Kind kind = Kind::Info;
	std::string title;
	std::string body;
	double start = -1.0; // set on the first frame it's drawn
};

static std::vector<Toast> g_toasts;
static int g_nextId = 0;

void Push(Kind kind, std::string title, std::string body)
{
	Toast toast;
	toast.id = g_nextId++;
	toast.kind = kind;
	toast.title = std::move(title);
	toast.body = std::move(body);
	g_toasts.push_back(std::move(toast));
	// Keep the newest few; older ones would be pushed off screen anyway.
	if (g_toasts.size() > kMaxVisible)
		g_toasts.erase(g_toasts.begin(), g_toasts.end() - kMaxVisible);
}

void Draw(bool reduceMotion)
{
	const double now = ImGui::GetTime();
	g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(), [now](const Toast& toast) {
		return toast.start >= 0.0 && now - toast.start > kLifetime;
	}), g_toasts.end());

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float margin = S(20.0f);
	const float width = S(340.0f);
	float bottom = viewport->WorkPos.y + viewport->WorkSize.y - margin;

	// Newest at the bottom; older ones stack above it.
	for (auto it = g_toasts.rbegin(); it != g_toasts.rend(); ++it) {
		Toast& toast = *it;
		if (toast.start < 0.0)
			toast.start = now;
		const double age = now - toast.start;
		float visibility = 1.0f;
		if (!reduceMotion) {
			const double fade = kFadeTime / Widgets::AnimationSpeed();
			const double fadeIn = std::min(1.0, age / fade);
			const double fadeOut = std::min(1.0, (kLifetime - age) / fade);
			visibility = static_cast<float>(std::clamp(std::min(fadeIn, fadeOut), 0.0, 1.0));
		}
		const float slide = (1.0f - visibility) * S(24.0f);

		const ImVec4 stripe = toast.kind == Kind::Error ? Theme::C().error
			: toast.kind == Kind::Success ? Theme::C().accent : Theme::C().info;

		char windowId[32];
		std::snprintf(windowId, sizeof(windowId), "##toast%d", toast.id);
		ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin + slide, bottom), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.98f);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, visibility);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(12.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(10.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::C().cardBg);
		ImGui::PushStyleColor(ImGuiCol_Border, Theme::Mix(Theme::C().border, stripe, 0.35f));
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_AlwaysAutoResize;
		if (ImGui::Begin(windowId, nullptr, flags)) {
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 pos = ImGui::GetWindowPos();
			const ImVec2 size = ImGui::GetWindowSize();
			drawList->AddRectFilled(ImVec2(pos.x + S(6.0f), pos.y + S(10.0f)), ImVec2(pos.x + S(10.0f), pos.y + size.y - S(10.0f)),
				ImGui::GetColorU32(stripe), S(2.0f));
			ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
			ImGui::TextUnformatted(toast.title.c_str());
			ImGui::PopFont();
			if (!toast.body.empty()) {
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled("%s", toast.body.c_str());
				ImGui::PopTextWrapPos();
			}
			bottom -= size.y + S(10.0f);
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);
	}
}

}

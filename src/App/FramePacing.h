#pragma once

// How long the launcher should take per frame. With vsync on and nothing else limiting it, the
// main loop redraws at the monitor's refresh rate forever and the graphics driver spins a CPU core
// while it waits for each refresh (measured at 100% of a core on a 240 Hz monitor with the window
// idle). So the loop sleeps between frames instead: full speed while the player is interacting,
// slower when nothing is happening. No ImGui or window code here, so the rules can be tested alone.
namespace FramePacing {

constexpr double kActiveSeconds = 1.0; // keep full speed this long after the last input, for scroll easing and fades
constexpr int kIdleFps = 30;           // idle frame rate: the mascot's bob and spinners still look smooth
constexpr int kBackgroundFps = 10;     // window unfocused or minimized

struct Input {
	int maxFps = 0;             // Settings::maxFps: 0 = as fast as the display refreshes
	bool lowerWhenIdle = true;  // Settings::lowerIdleFrameRate
	bool focused = true;
	bool minimized = false;
	double idleSeconds = 0.0;   // time since the last input or visible change
};

struct Pace {
	double interval = 0.0; // seconds per frame; 0 = don't wait, vsync paces the loop
	bool idle = false;     // slowed down because nothing is happening: wake at once on any event
};

Pace Choose(const Input& input);

}

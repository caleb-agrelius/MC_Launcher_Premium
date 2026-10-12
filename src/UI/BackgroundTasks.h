#pragma once

#include <functional>

// How a page runs slow work (network, disk) without blocking the UI: run() starts it on a
// worker thread, and the worker hands its results to post(), which runs them on the UI
// thread at the start of the next frame. main.cpp makes one for the app's lifetime.
struct BackgroundTasks {
	std::function<void(std::function<void()>)> run;
	std::function<void(std::function<void()>)> post;
};

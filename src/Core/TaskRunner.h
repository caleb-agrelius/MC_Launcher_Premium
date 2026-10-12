#pragma once

#include <functional>

// Where slow work runs and where its results land. Services depend on these two interfaces,
// never on a thread type or on the UI toolkit, so the same code runs under the app's worker
// pool and UI loop and under a test's synchronous stand-ins.

// Runs a task off the UI thread.
class ITaskRunner {
public:
	virtual ~ITaskRunner() = default;
	virtual void Run(std::function<void()> task) = 0;
};

// Hands a task to the UI thread, which runs it at the start of its next frame. Anything a
// worker wants to change on screen (or in UI-thread-only state) goes through this.
class IUiPoster {
public:
	virtual ~IUiPoster() = default;
	virtual void Post(std::function<void()> task) = 0;
};

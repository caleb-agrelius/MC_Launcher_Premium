#pragma once

#include "Core/TaskRunner.h"

#include <functional>
#include <mutex>
#include <vector>

// Work handed from background threads to the UI thread. Drain() runs it at the start of a
// frame, so UI state (pack list, selection) is only touched on the UI thread.
class UiQueue final : public IUiPoster {
public:
	void Post(std::function<void()> task) override;

	// Runs everything posted so far, in order. Tasks posted while draining wait for the next call.
	void Drain();

private:
	std::mutex m_mutex;
	std::vector<std::function<void()>> m_tasks;
};

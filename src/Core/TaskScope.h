#pragma once

#include "Core/TaskRunner.h"

#include <condition_variable>
#include <functional>
#include <mutex>

// The background tasks one service owns. A task started through a scope may use the service
// that owns the scope (it usually captures `this`), because the scope's destructor waits for
// every task it started to finish. That is what keeps a worker from outliving the object it
// works on. Whoever destroys the owner must first tell the tasks to stop (the app's shutdown
// flag), or the wait lasts as long as the slowest task.
class TaskScope {
public:
	explicit TaskScope(ITaskRunner& runner) : m_runner(runner) {}
	~TaskScope() { Wait(); }
	TaskScope(const TaskScope&) = delete;
	TaskScope& operator=(const TaskScope&) = delete;

	void Run(std::function<void()> task);

	// Blocks until every task started so far has finished.
	void Wait();

private:
	ITaskRunner& m_runner;
	std::mutex m_mutex;
	std::condition_variable m_idle;
	int m_pending = 0;
};

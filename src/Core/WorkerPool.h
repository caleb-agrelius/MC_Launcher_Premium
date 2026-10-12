#pragma once

#include "Core/TaskRunner.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// One thread per task, joined once the task is done. The UI loop calls Reap() each frame to
// drop finished threads instead of keeping them until exit, and JoinAll() at shutdown.
class WorkerPool final : public ITaskRunner {
public:
	// onThreadExit runs on a worker thread after its task, before the pool counts the thread
	// as finished, e.g. to free that thread's network handles so joining never waits on them.
	explicit WorkerPool(std::function<void()> onThreadExit = {});
	~WorkerPool();
	WorkerPool(const WorkerPool&) = delete;
	WorkerPool& operator=(const WorkerPool&) = delete;

	// An exception escaping a task is logged and swallowed: it would otherwise end the whole
	// app. Tasks that report errors to the UI catch their own; this is the last resort.
	void Run(std::function<void()> task) override;

	// Joins and forgets the threads whose tasks have finished.
	void Reap();

	// Waits for every task to finish. Tasks must have been told to stop first.
	void JoinAll();

private:
	struct Worker {
		std::thread thread;
		std::shared_ptr<std::atomic<bool>> done;
	};

	std::function<void()> m_onThreadExit;
	std::mutex m_mutex;
	std::vector<Worker> m_workers;
};

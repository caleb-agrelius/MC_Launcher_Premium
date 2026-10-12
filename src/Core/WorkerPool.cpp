#include "Core/WorkerPool.h"

#include <cstdio>
#include <exception>
#include <utility>

WorkerPool::WorkerPool(std::function<void()> onThreadExit)
	: m_onThreadExit(std::move(onThreadExit))
{
}

WorkerPool::~WorkerPool()
{
	JoinAll();
}

void WorkerPool::Run(std::function<void()> task)
{
	Worker worker;
	worker.done = std::make_shared<std::atomic<bool>>(false);
	worker.thread = std::thread([task = std::move(task), done = worker.done, onExit = m_onThreadExit]() {
		try {
			task();
		} catch (const std::exception& ex) {
			std::fprintf(stderr, "Background task failed: %s\n", ex.what());
		} catch (...) {
			std::fprintf(stderr, "Background task failed\n");
		}
		if (onExit)
			onExit();
		done->store(true);
	});
	std::lock_guard<std::mutex> lock(m_mutex);
	m_workers.push_back(std::move(worker));
}

void WorkerPool::Reap()
{
	std::vector<Worker> finished;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto it = m_workers.begin(); it != m_workers.end();) {
			if (it->done->load()) {
				finished.push_back(std::move(*it));
				it = m_workers.erase(it);
			} else {
				++it;
			}
		}
	}
	for (Worker& worker : finished)
		worker.thread.join();
}

void WorkerPool::JoinAll()
{
	std::vector<Worker> all;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		all.swap(m_workers);
	}
	for (Worker& worker : all) {
		if (worker.thread.joinable())
			worker.thread.join();
	}
}

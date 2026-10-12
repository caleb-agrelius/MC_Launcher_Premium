#include "Core/UiQueue.h"

#include <utility>

void UiQueue::Post(std::function<void()> task)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_tasks.push_back(std::move(task));
}

void UiQueue::Drain()
{
	std::vector<std::function<void()>> pending;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		pending.swap(m_tasks);
	}
	for (auto& task : pending)
		task();
}

#include "Core/TaskScope.h"

#include <utility>

void TaskScope::Run(std::function<void()> task)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_pending;
	}
	m_runner.Run([this, task = std::move(task)]() {
		// Counted down even if the task throws, so Wait() can't hang on it.
		struct Finish {
			TaskScope* scope;
			~Finish()
			{
				std::lock_guard<std::mutex> lock(scope->m_mutex);
				--scope->m_pending;
				scope->m_idle.notify_all(); // under the lock, so Wait() can't return and destroy the scope first
			}
		} finish{ this };
		task();
	});
}

void TaskScope::Wait()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_idle.wait(lock, [this]() { return m_pending == 0; });
}

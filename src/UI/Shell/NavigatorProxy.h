#pragma once

#include "UI/Navigation.h"

// An INavigator that hands every request to the real one once it exists. The pages need a
// navigator when they are built, but the shell that navigates is built from the pages; this
// breaks the loop. Requests before Bind() are dropped.
class NavigatorProxy final : public INavigator {
public:
	void Bind(INavigator& target) { m_target = &target; }

	void ShowPage(Page page) override { if (m_target) m_target->ShowPage(page); }
	void ShowManage(const std::string& packId, bool browse = false) override { if (m_target) m_target->ShowManage(packId, browse); }
	void ShowServer(const std::string& packId) override { if (m_target) m_target->ShowServer(packId); }

private:
	INavigator* m_target = nullptr;
};

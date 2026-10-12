#include "Services/Settings/JavaScanService.h"

#include "Services/AppPaths.h"

#include <filesystem>

namespace fs = std::filesystem;

JavaScanService::JavaScanService(const Runtime& runtime)
	: m_rt(runtime), m_scope(runtime.tasks)
{
}

void JavaScanService::Scan()
{
	if (m_scanning)
		return;
	m_scanning = true;
	std::error_code ec;
	m_scope.Run([this, root = fs::absolute(AppPaths::kGameRoot, ec)]() {
		std::vector<JavaInstall> found = FindJavaInstalls(root);
		m_rt.ui.Post([this, found = std::move(found)]() mutable {
			m_installs = std::move(found);
			m_scanning = false;
			m_scanned = true;
		});
	});
}

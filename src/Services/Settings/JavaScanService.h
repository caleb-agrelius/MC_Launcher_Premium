#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Game/JavaFinder.h"

#include <vector>

// The Javas installed on this PC, for Settings' Java choice. Found on a worker (a scan walks
// the registry and several folders) when the Settings page first asks. UI thread only.
class JavaScanService {
public:
	JavaScanService(const Runtime& runtime);

	// Starts a scan unless one is running. Results replace the list when it finishes.
	void Scan();

	const std::vector<JavaInstall>& Installs() const { return m_installs; }
	bool Scanning() const { return m_scanning; }
	bool Scanned() const { return m_scanned; }

private:
	Runtime m_rt;
	std::vector<JavaInstall> m_installs;
	bool m_scanning = false;
	bool m_scanned = false;
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};

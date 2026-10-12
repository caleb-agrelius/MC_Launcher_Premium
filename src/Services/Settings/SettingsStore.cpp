#include "Services/Settings/SettingsStore.h"

#include <algorithm>
#include <utility>

SettingsStore::SettingsStore(std::string file, std::ostream& log)
	: m_file(std::move(file)), m_log(log), m_settings(LoadSettings(m_file))
{
}

void SettingsStore::SaveIfSettled(bool editingNow)
{
	if (m_dirty && !editingNow) {
		m_dirty = false;
		if (!SaveSettings(m_file, m_settings))
			m_log << "Could not save " << m_file << "\n";
	}
}

void SettingsStore::SaveNow()
{
	m_dirty = false;
	if (!SaveSettings(m_file, m_settings))
		m_log << "Could not save " << m_file << "\n";
}

float SettingsStore::PaneWidth(const std::string& pane, float fallback, float maxWidth) const
{
	const auto it = m_settings.paneWidths.find(pane);
	const float width = it != m_settings.paneWidths.end() ? it->second : fallback;
	return std::clamp(width, std::min(kMinPaneWidth, maxWidth), std::max(kMinPaneWidth, maxWidth));
}

void SettingsStore::SetPaneWidth(const std::string& pane, float width)
{
	m_settings.paneWidths[pane] = width;
	m_dirty = true; // written once the drag ends
}

#pragma once

#include "Storage/Settings.h"

#include <ostream>
#include <string>

// The launcher's settings: loaded once, edited in place by the Settings page and by anything
// else that remembers a preference (pane widths, Discover's source filter), and saved to
// settings.json after the edits settle. UI thread only.
class SettingsStore {
public:
	SettingsStore(std::string file, std::ostream& log);

	Settings& Get() { return m_settings; }
	const Settings& Get() const { return m_settings; }

	// Asks for a save. It happens in SaveIfSettled, not right away.
	void MarkDirty() { m_dirty = true; }

	// Saves pending edits unless the player is still editing (a text box or slider is active),
	// so the file isn't rewritten on every keystroke or slider step.
	void SaveIfSettled(bool editingNow);

	// Saves regardless; the file is written even when nothing changed.
	void SaveNow();

	// A resizable pane's width (unscaled): the saved one or fallback, kept within what fits.
	float PaneWidth(const std::string& pane, float fallback, float maxWidth) const;
	void SetPaneWidth(const std::string& pane, float width);

private:
	std::string m_file;
	std::ostream& m_log;
	Settings m_settings;
	bool m_dirty = false;
};

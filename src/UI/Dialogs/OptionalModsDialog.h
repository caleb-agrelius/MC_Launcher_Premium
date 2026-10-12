#pragma once

class OptionalModsService;

// "Optional mods": pick the extras to install with a pack (ATLauncher) before it installs.
class OptionalModsDialog {
public:
	explicit OptionalModsDialog(OptionalModsService& optional) : m_optional(optional) {}

	void Draw();

private:
	OptionalModsService& m_optional;
};

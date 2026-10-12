#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Model/Modpack.h"
#include "Network/PackSources.h"
#include "Services/Downloads/DownloadService.h"
#include "Services/Library/PackLibrary.h"

#include <cstddef>
#include <string>
#include <vector>

// Asks for a whole pack to be installed. Pages call this and don't care whether the pack
// installs straight away or first offers its optional mods.
class IInstallRequester {
public:
	virtual ~IInstallRequester() = default;
	virtual void RequestInstall(const Modpack& pack) = 0;
};

// The optional-mods picker shown before installing a pack that has some (ATLauncher).
struct OptionalPicker {
	Modpack pack;               // empty id = closed
	bool loading = false;
	std::string error;
	std::vector<OptionalMod> mods;
	std::vector<char> chosen;   // parallel to mods
	int generation = 0;
};

// Install pack: ATLauncher packs first offer their optional mods; everything else installs right
// away. Owns the picker's state and keeps the player's choice consistent. UI thread only.
class OptionalModsService final : public IInstallRequester {
public:
	OptionalModsService(const Runtime& runtime, PackLibrary& library, DownloadService& downloads);

	void RequestInstall(const Modpack& pack) override;

	bool IsOpen() const { return !m_picker.pack.id.empty(); }
	const OptionalPicker& Picker() const { return m_picker; }

	// Checks or unchecks an optional mod, keeping the choice consistent: checking adds the mods
	// it depends on and unchecks others in its group; unchecking drops mods that need it.
	void SetOptional(std::size_t index, bool on);
	// Installs the pack with the chosen mods and closes the picker.
	void Confirm();
	void Cancel();

private:
	Runtime m_rt;
	PackLibrary& m_library;
	DownloadService& m_downloads;
	OptionalPicker m_picker;
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};

#pragma once

#include "UI/MobModel.h"
#include "UI/Mobs.h"

#include <filesystem>
#include <ostream>
#include <string>

// The sidebar mascot: a random mob each launch; right-click it for another.
class Mascot {
public:
	// foxModel is the bundled glTF fox; the other mobs are built from boxes.
	Mascot(std::filesystem::path foxModel, std::ostream& log) : m_foxModel(std::move(foxModel)), m_log(log) {}

	// Picks a mob that loads (trying a few), different from the last one.
	void Roll();
	void SetReduceMotion(bool reduce) { m_model.SetReduceMotion(reduce); m_reduceMotion = reduce; }
	// Draws it as a size x size item; right-clicking rolls another.
	void Draw(float size);
	// Frees GL resources while the context still exists.
	void Shutdown() { m_model.Shutdown(); }

private:
	std::filesystem::path m_foxModel;
	std::ostream& m_log;
	MobModel m_model;
	Mob m_mob = Mob::Count;
	std::string m_tooltip;
	bool m_reduceMotion = false;
};

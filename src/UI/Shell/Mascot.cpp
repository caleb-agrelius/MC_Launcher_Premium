#include "UI/Shell/Mascot.h"

#include <array>
#include <vector>

void Mascot::Roll()
{
	for (int attempt = 0; attempt < 3; ++attempt) {
		const Mob mob = RandomMob(m_mob);
		m_mob = mob; // a failed mob isn't picked again on the next attempt
		std::string error;
		bool loaded = false;
		if (mob == Mob::Fox) {
			loaded = !m_foxModel.empty() && m_model.Load(m_foxModel.string(), &error);
		} else {
			std::vector<MobBox> boxes;
			std::array<float, 3> pivot{};
			loaded = BuildMobBoxes(mob, boxes, pivot) && m_model.LoadBoxes(boxes, pivot, &error);
		}
		if (loaded) {
			m_model.SetReduceMotion(m_reduceMotion);
			m_model.SetIdleBob(MobIdleBob(mob));
			m_model.SetSizeFactor(MobSizeFactor(mob));
			m_tooltip = std::string(MobName(mob)) + "\nClick to make it hop. Right-click for another mob.";
			m_log << "Mascot: " << MobName(mob) << "\n";
			return;
		}
		m_log << "Mascot " << MobName(mob) << " unavailable: " << (error.empty() ? "model file not found" : error) << "\n";
	}
}

void Mascot::Draw(float size)
{
	if (m_model.Draw(size, m_tooltip.c_str()))
		Roll();
}

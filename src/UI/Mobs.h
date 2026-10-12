#pragma once

#include "MobModel.h"

#include <array>
#include <string>
#include <vector>

// The mobs the sidebar mascot can be. The fox is a bundled glTF model; the others are
// built from boxes in Minecraft's proportions.
enum class Mob { Fox, Pig, Creeper, Cow, Chicken, Sheep, Slime, Bee, Count };

const char* MobName(Mob mob);

// Fills boxes and the head pivot for a box-built mob. Returns false for the fox,
// which is loaded from its .glb instead.
bool BuildMobBoxes(Mob mob, std::vector<MobBox>& boxes, std::array<float, 3>& headPivot);

// Idle bobbing for mobs that hover or wobble (bee, slime); 0 for the rest.
float MobIdleBob(Mob mob);

// How big the mob is drawn relative to the default, which fits its longest side.
float MobSizeFactor(Mob mob);

// A random mob, different from `previous` when there's a choice.
Mob RandomMob(Mob previous = Mob::Count);

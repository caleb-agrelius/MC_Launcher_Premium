#include "Mobs.h"

#include <random>

namespace {

using Color = std::array<float, 4>;

constexpr Color Rgb(int r, int g, int b)
{
	return { r / 255.0f, g / 255.0f, b / 255.0f, 1.0f };
}

// Boxes are given as x0..x1 (back to front), y0..y1 (up), z0..z1 (side to side).
struct Builder {
	std::vector<MobBox>& boxes;
	void Add(float x0, float x1, float y0, float y1, float z0, float z1, const Color& color, bool head = false)
	{
		boxes.push_back({ { x0, y0, z0 }, { x1, y1, z1 }, color, head });
	}
	// A face detail sitting just in front of the face at x (eyes, mouths, snouts).
	void Face(float x, float y0, float y1, float z0, float z1, const Color& color, bool head = true)
	{
		Add(x, x + 0.12f, y0, y1, z0, z1, color, head);
	}
};

const Color kBlack = Rgb(24, 24, 24);
const Color kWhite = Rgb(240, 240, 236);

void Pig(Builder& b)
{
	const Color pink = Rgb(242, 166, 166);
	const Color legPink = Rgb(226, 146, 150);
	const Color snout = Rgb(226, 128, 140);
	for (float x : { -7.0f, 3.0f })
		for (float z : { -5.0f, 1.0f })
			b.Add(x, x + 4, 0, 6, z, z + 4, legPink);
	b.Add(-8, 8, 6, 14, -5, 5, pink);
	b.Add(7, 15, 8, 16, -4, 4, pink, true);
	b.Add(15, 16, 9, 12, -2, 2, snout, true);
	b.Face(16, 10, 11, -1.5f, -0.5f, Rgb(150, 70, 80));
	b.Face(16, 10, 11, 0.5f, 1.5f, Rgb(150, 70, 80));
	b.Face(15, 12.5f, 13.5f, -3, -2, kWhite);
	b.Face(15, 12.5f, 13.5f, -2, -1, kBlack);
	b.Face(15, 12.5f, 13.5f, 1, 2, kBlack);
	b.Face(15, 12.5f, 13.5f, 2, 3, kWhite);
}

void Creeper(Builder& b)
{
	const Color green = Rgb(94, 186, 80);
	const Color dark = Rgb(62, 140, 56);
	const Color face = Rgb(18, 26, 18);
	for (float x : { -6.0f, 2.0f })
		for (float z : { -4.0f, 0.0f })
			b.Add(x, x + 4, 0, 6, z, z + 4, dark);
	b.Add(-2, 2, 6, 18, -4, 4, green);
	// A few darker pixels for the creeper's mottled look.
	b.Add(-2.1f, 2.1f, 9, 11, -2, 0, dark);
	b.Add(-2.1f, 2.1f, 13, 15, 1, 3, dark);
	b.Add(-4, 4, 18, 26, -4, 4, green, true);
	b.Add(-4.1f, -1, 23, 25, -4.1f, 4.1f, dark, true);
	// The face, row by row from the classic 8x8 texture.
	b.Face(4, 23, 25, -3, -1, face);
	b.Face(4, 23, 25, 1, 3, face);
	b.Face(4, 22, 23, -1, 1, face);
	b.Face(4, 20, 22, -2, 2, face);
	b.Face(4, 19, 20, -2, -1, face);
	b.Face(4, 19, 20, 1, 2, face);
}

void Cow(Builder& b)
{
	const Color brown = Rgb(92, 64, 46);
	const Color patch = Rgb(232, 228, 220);
	const Color snout = Rgb(214, 182, 168);
	const Color horn = Rgb(220, 214, 196);
	for (float x : { -8.0f, 4.0f })
		for (float z : { -6.0f, 2.0f })
			b.Add(x, x + 4, 0, 12, z, z + 4, brown);
	b.Add(-9, 9, 12, 22, -6, 6, brown);
	// Patches poke slightly through both sides of the body.
	b.Add(-6, 0, 15, 20, -6.1f, 6.1f, patch);
	b.Add(3, 7, 13, 17, -6.1f, 6.1f, patch);
	b.Add(8, 14, 16, 24, -4, 4, brown, true);
	b.Add(14, 15, 16, 20, -2, 2, snout, true);
	b.Add(11, 12, 22, 25, -5, -4, horn, true);
	b.Add(11, 12, 22, 25, 4, 5, horn, true);
	b.Face(14, 21, 22, -3, -1.5f, kBlack);
	b.Face(14, 21, 22, 1.5f, 3, kBlack);
	b.Face(15, 17.5f, 18.5f, -1.5f, -0.5f, Rgb(120, 80, 70));
	b.Face(15, 17.5f, 18.5f, 0.5f, 1.5f, Rgb(120, 80, 70));
}

void Chicken(Builder& b)
{
	const Color feathers = Rgb(244, 244, 240);
	const Color wing = Rgb(222, 222, 216);
	const Color orange = Rgb(240, 160, 40);
	const Color red = Rgb(214, 40, 40);
	b.Add(-0.5f, 0.5f, 0, 5, -2, -1, orange);
	b.Add(-0.5f, 0.5f, 0, 5, 1, 2, orange);
	b.Add(-1, 2, 0, 0.6f, -2.5f, -0.5f, orange);
	b.Add(-1, 2, 0, 0.6f, 0.5f, 2.5f, orange);
	b.Add(-4, 4, 5, 11, -3, 3, feathers);
	b.Add(-3, 3, 6, 10, -3.6f, 3.6f, wing);
	b.Add(3, 6, 9, 15, -2, 2, feathers, true);
	b.Add(6, 8, 12, 14, -2, 2, orange, true);
	b.Add(6, 7, 10, 12, -1, 1, red, true);
	b.Add(4, 5, 13, 14, -2.12f, 2.12f, kBlack, true);
}

void Sheep(Builder& b)
{
	const Color wool = Rgb(238, 236, 230);
	const Color skin = Rgb(196, 172, 150);
	for (float x : { -7.0f, 3.0f })
		for (float z : { -5.0f, 1.0f })
			b.Add(x, x + 4, 0, 12, z, z + 4, skin);
	for (float x : { -7.2f, 2.8f })
		for (float z : { -5.2f, 0.8f })
			b.Add(x, x + 4.4f, 7, 12, z, z + 4.4f, wool);
	b.Add(-8, 8, 11, 21, -5, 5, wool);
	b.Add(7, 13, 14, 20, -3, 3, skin, true);
	b.Add(7.5f, 12, 18, 21, -3.4f, 3.4f, wool, true);
	b.Face(13, 16.5f, 17.5f, -2.5f, -1.5f, kBlack);
	b.Face(13, 16.5f, 17.5f, 1.5f, 2.5f, kBlack);
	b.Face(13, 14.5f, 15.5f, -1, 1, Rgb(220, 160, 160));
}

void Slime(Builder& b)
{
	const Color outer = Rgb(112, 192, 92);
	const Color inner = Rgb(80, 156, 64);
	b.Add(-6, 6, 0, 12, -6, 6, outer, true);
	b.Face(6, 7, 9, -4, -2, inner);
	b.Face(6, 7, 9, 2, 4, inner);
	b.Face(6.05f, 8, 9, -4, -3, Rgb(30, 60, 30));
	b.Face(6.05f, 8, 9, 3, 4, Rgb(30, 60, 30));
	b.Face(6, 4, 5, 0, 1, inner);
}

void Bee(Builder& b)
{
	const Color yellow = Rgb(240, 196, 64);
	const Color stripe = Rgb(70, 46, 30);
	const Color wing = Rgb(214, 232, 244);
	b.Add(-5, 5, 3, 10, -3.5f, 3.5f, yellow, true);
	b.Add(-1, 1, 2.9f, 10.1f, -3.6f, 3.6f, stripe, true);
	b.Add(-4, -2.5f, 2.9f, 10.1f, -3.6f, 3.6f, stripe, true);
	b.Add(-6, -5, 6, 7, -0.5f, 0.5f, Rgb(120, 120, 120), true);
	b.Face(5, 6, 9, -3.5f, -1.5f, kBlack);
	b.Face(5, 6, 9, 1.5f, 3.5f, kBlack);
	b.Add(5, 7, 10, 10.6f, -2, -1.4f, stripe, true);
	b.Add(5, 7, 10, 10.6f, 1.4f, 2, stripe, true);
	b.Add(-2, 3, 10, 10.4f, -7, -1, wing, true);
	b.Add(-2, 3, 10, 10.4f, 1, 7, wing, true);
	for (float x : { -2.0f, 0.5f, 3.0f })
		b.Add(x, x + 1, 1.5f, 3, -1.5f, 1.5f, stripe, true);
}

} // namespace

const char* MobName(Mob mob)
{
	switch (mob) {
	case Mob::Fox: return "Fox";
	case Mob::Pig: return "Pig";
	case Mob::Creeper: return "Creeper";
	case Mob::Cow: return "Cow";
	case Mob::Chicken: return "Chicken";
	case Mob::Sheep: return "Sheep";
	case Mob::Slime: return "Slime";
	case Mob::Bee: return "Bee";
	default: return "Mob";
	}
}

bool BuildMobBoxes(Mob mob, std::vector<MobBox>& boxes, std::array<float, 3>& headPivot)
{
	boxes.clear();
	Builder b{ boxes };
	switch (mob) {
	case Mob::Pig: Pig(b); headPivot = { 7, 12, 0 }; break;
	case Mob::Creeper: Creeper(b); headPivot = { 0, 18, 0 }; break;
	case Mob::Cow: Cow(b); headPivot = { 8, 20, 0 }; break;
	case Mob::Chicken: Chicken(b); headPivot = { 4, 10, 0 }; break;
	case Mob::Sheep: Sheep(b); headPivot = { 7, 17, 0 }; break;
	case Mob::Slime: Slime(b); headPivot = { 0, 0, 0 }; break;
	case Mob::Bee: Bee(b); headPivot = { 0, 6, 0 }; break;
	default: return false;
	}
	return true;
}

float MobIdleBob(Mob mob)
{
	if (mob == Mob::Bee)
		return 0.06f;
	if (mob == Mob::Slime)
		return 0.03f;
	return 0.0f;
}

float MobSizeFactor(Mob mob)
{
	switch (mob) {
	case Mob::Slime: return 0.62f; // a cube looks much bigger than long mobs of the same extent
	case Mob::Creeper: return 1.08f;
	case Mob::Chicken: return 0.92f;
	default: return 1.0f;
	}
}

Mob RandomMob(Mob previous)
{
	static std::mt19937 rng{ std::random_device{}() };
	const int count = static_cast<int>(Mob::Count);
	std::uniform_int_distribution<int> pick(0, count - 1);
	Mob mob = static_cast<Mob>(pick(rng));
	if (mob == previous)
		mob = static_cast<Mob>((static_cast<int>(mob) + 1 + pick(rng) % (count - 1)) % count);
	return mob;
}

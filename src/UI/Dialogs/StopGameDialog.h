#pragma once

#include <string>

class PlayService;

// "Stop Minecraft?": confirms before force-closing a running game.
class StopGameDialog {
public:
	explicit StopGameDialog(PlayService& plays) : m_plays(plays) {}

	void Open(const std::string& playKey) { m_playKey = playKey; }
	void Draw();

private:
	PlayService& m_plays;
	std::string m_playKey; // empty = closed
};

#pragma once

#include "imgui.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// One solid-colored box of a mob built in code (see Mobs.h). Units are Minecraft
// model pixels; +X is the front (like the fox model), +Y is up.
struct MobBox {
	std::array<float, 3> min;
	std::array<float, 3> max;
	std::array<float, 4> color;
	bool head = false; // turns with the head to look at the mouse
};

// The interactive mascot in the sidebar: a mob that turns to follow the mouse and
// hops when clicked. Rendered into an offscreen texture with its own small shader.
// The model is either a glTF binary (the fox) or a list of boxes.
class MobModel {
public:
	MobModel() = default;
	~MobModel();

	MobModel(const MobModel&) = delete;
	MobModel& operator=(const MobModel&) = delete;

	// Loads a .glb model. Its node named "Cube_0" is treated as the head.
	bool Load(const std::string& path, std::string* error = nullptr);
	// Builds the model from boxes. headPivot is where the head turns around.
	bool LoadBoxes(const std::vector<MobBox>& boxes, const std::array<float, 3>& headPivot, std::string* error = nullptr);
	void SetReduceMotion(bool reduce);
	// Idle bobbing amplitude (in model units after scaling), e.g. for a hovering bee.
	void SetIdleBob(float amount) { m_idleBob = amount; }
	// Draws the mob bigger or smaller than the default fit.
	void SetSizeFactor(float factor) { m_sizeFactor = factor; }
	// Draws the mob as a size x size item. Returns true when it was right-clicked
	// (the caller rolls a new mob). tooltip is shown on hover.
	bool Draw(float size, const char* tooltip = nullptr);
	void Shutdown();

private:
	struct Part {
		unsigned int vertexArray = 0;
		unsigned int vertexBuffer = 0;
		unsigned int indexBuffer = 0;
		int indexCount = 0;
		unsigned int indexType = 0;
		std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
	};
	struct DrawPart {
		std::size_t part = 0;
		std::size_t node = 0;
		bool head = false;
	};

	bool CreateProgram(std::string* error);
	bool Finish(const std::array<float, 3>& minimum, const std::array<float, 3>& maximum, std::string* error);
	bool CreateRenderTarget(std::string* error);
	bool Render(std::string* error);
	void DestroyRenderTarget();

	std::vector<Part> m_parts;
	std::vector<DrawPart> m_drawParts;
	std::vector<std::array<float, 16>> m_nodeMatrices;
	std::array<float, 3> m_center{ 0.0f, 0.0f, 0.0f };
	std::array<float, 3> m_headPivot{ 0.0f, 0.0f, 0.0f };
	float m_scale = 1.0f;

	unsigned int m_program = 0;
	unsigned int m_framebuffer = 0;
	unsigned int m_colorTexture = 0;
	unsigned int m_depthBuffer = 0;
	int m_renderSize = 0;
	int m_modelViewProjectionLocation = -1;
	int m_modelLocation = -1;
	int m_colorLocation = -1;

	float m_headYaw = 0.0f;
	float m_headPitch = 0.0f;
	float m_targetHeadYaw = 0.0f;
	float m_targetHeadPitch = 0.0f;
	float m_bodyYaw = 0.0f;
	float m_bodyPitch = 0.0f;
	float m_targetBodyYaw = 0.0f;
	float m_targetBodyPitch = 0.0f;
	float m_hopTime = -1.0f;
	float m_idleBob = 0.0f;
	float m_sizeFactor = 1.0f;
	bool m_reduceMotion = false;
	bool m_loaded = false;
	std::string m_loadError;
};

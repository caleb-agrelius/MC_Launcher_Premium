#pragma once

#include "imgui.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class FoxModel {
public:
	FoxModel() = default;
	~FoxModel();

	FoxModel(const FoxModel&) = delete;
	FoxModel& operator=(const FoxModel&) = delete;

	bool Load(const std::string& path, std::string* error = nullptr);
	void SetReduceMotion(bool reduce);
	void Draw(float size);
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
	};

	bool CreateRenderTarget(std::string* error);
	bool Render(std::string* error);
	void DestroyRenderTarget();

	std::vector<Part> m_parts;
	std::vector<DrawPart> m_drawParts;
	std::vector<std::array<float, 16>> m_nodeMatrices;
	std::array<float, 3> m_center{ 0.0f, 0.0f, 0.0f };
	float m_scale = 1.0f;
	std::size_t m_headNode = static_cast<std::size_t>(-1);

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
	bool m_reduceMotion = false;
	bool m_loaded = false;
	std::string m_loadError;
};

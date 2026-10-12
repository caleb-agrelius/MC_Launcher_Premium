#include "FoxModel.h"

#include "Theme.h"

#include <glad/glad.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <utility>

using json = nlohmann::json;

namespace {

constexpr std::uint32_t kGlbMagic = 0x46546C67;
constexpr std::uint32_t kJsonChunk = 0x4E4F534A;
constexpr std::uint32_t kBinChunk = 0x004E4942;
constexpr int kRenderSize = 256;
constexpr float kPi = 3.14159265358979323846f;

struct Vertex {
	float position[3];
	float normal[3];
};

using Mat4 = std::array<float, 16>;

Mat4 Identity()
{
	return { 1, 0, 0, 0,
			 0, 1, 0, 0,
			 0, 0, 1, 0,
			 0, 0, 0, 1 };
}

Mat4 Multiply(const Mat4& a, const Mat4& b)
{
	Mat4 result{};
	for (int column = 0; column < 4; ++column)
		for (int row = 0; row < 4; ++row)
			for (int k = 0; k < 4; ++k)
				result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
	return result;
}

Mat4 Translation(float x, float y, float z)
{
	Mat4 result = Identity();
	result[12] = x;
	result[13] = y;
	result[14] = z;
	return result;
}

Mat4 Scaling(float x, float y, float z)
{
	Mat4 result = Identity();
	result[0] = x;
	result[5] = y;
	result[10] = z;
	return result;
}

Mat4 RotationX(float angle)
{
	Mat4 result = Identity();
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	result[5] = c;
	result[6] = s;
	result[9] = -s;
	result[10] = c;
	return result;
}

Mat4 RotationY(float angle)
{
	Mat4 result = Identity();
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	result[0] = c;
	result[2] = -s;
	result[8] = s;
	result[10] = c;
	return result;
}

Mat4 RotationZ(float angle)
{
	Mat4 result = Identity();
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	result[0] = c;
	result[1] = s;
	result[4] = -s;
	result[5] = c;
	return result;
}

Mat4 Perspective(float verticalFov, float aspect, float nearPlane, float farPlane)
{
	const float f = 1.0f / std::tan(verticalFov * 0.5f);
	Mat4 result{};
	result[0] = f / aspect;
	result[5] = f;
	result[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
	result[11] = -1.0f;
	result[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
	return result;
}

Mat4 LookAt(float eyeX, float eyeY, float eyeZ, float targetX, float targetY, float targetZ)
{
	auto normalize = [](std::array<float, 3> v) {
		const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (length > 0.0f)
			for (float& value : v)
				value /= length;
		return v;
	};
	auto cross = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
		return std::array<float, 3>{
			a[1] * b[2] - a[2] * b[1],
			a[2] * b[0] - a[0] * b[2],
			a[0] * b[1] - a[1] * b[0]
		};
	};

	const auto forward = normalize({ targetX - eyeX, targetY - eyeY, targetZ - eyeZ });
	const auto side = normalize(cross(forward, { 0.0f, 1.0f, 0.0f }));
	const auto up = cross(side, forward);
	Mat4 result = Identity();
	result[0] = side[0];
	result[1] = up[0];
	result[2] = -forward[0];
	result[4] = side[1];
	result[5] = up[1];
	result[6] = -forward[1];
	result[8] = side[2];
	result[9] = up[2];
	result[10] = -forward[2];
	result[12] = -(side[0] * eyeX + side[1] * eyeY + side[2] * eyeZ);
	result[13] = -(up[0] * eyeX + up[1] * eyeY + up[2] * eyeZ);
	result[14] = forward[0] * eyeX + forward[1] * eyeY + forward[2] * eyeZ;
	return result;
}

Mat4 NodeTransform(const json& node)
{
	if (node.contains("matrix")) {
		const auto& values = node["matrix"];
		Mat4 result{};
		for (std::size_t i = 0; i < result.size(); ++i)
			result[i] = values.at(i).get<float>();
		return result;
	}

	std::array<float, 3> translation{ 0.0f, 0.0f, 0.0f };
	std::array<float, 4> rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
	std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
	if (node.contains("translation"))
		for (std::size_t i = 0; i < translation.size(); ++i)
			translation[i] = node["translation"].at(i).get<float>();
	if (node.contains("rotation"))
		for (std::size_t i = 0; i < rotation.size(); ++i)
			rotation[i] = node["rotation"].at(i).get<float>();
	if (node.contains("scale"))
		for (std::size_t i = 0; i < scale.size(); ++i)
			scale[i] = node["scale"].at(i).get<float>();

	const float x = rotation[0], y = rotation[1], z = rotation[2], w = rotation[3];
	Mat4 r = Identity();
	r[0] = 1.0f - 2.0f * (y * y + z * z);
	r[1] = 2.0f * (x * y + z * w);
	r[2] = 2.0f * (x * z - y * w);
	r[4] = 2.0f * (x * y - z * w);
	r[5] = 1.0f - 2.0f * (x * x + z * z);
	r[6] = 2.0f * (y * z + x * w);
	r[8] = 2.0f * (x * z + y * w);
	r[9] = 2.0f * (y * z - x * w);
	r[10] = 1.0f - 2.0f * (x * x + y * y);
	return Multiply(Translation(translation[0], translation[1], translation[2]),
		Multiply(r, Scaling(scale[0], scale[1], scale[2])));
}

Mat4 TransformPosition(const Mat4& matrix, const std::array<float, 3>& p)
{
	return {
		matrix[0] * p[0] + matrix[4] * p[1] + matrix[8] * p[2] + matrix[12],
		matrix[1] * p[0] + matrix[5] * p[1] + matrix[9] * p[2] + matrix[13],
		matrix[2] * p[0] + matrix[6] * p[1] + matrix[10] * p[2] + matrix[14],
		0
	};
}

bool ReadU32(std::ifstream& file, std::uint32_t& value)
{
	std::array<unsigned char, 4> bytes{};
	if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
		return false;
	value = static_cast<std::uint32_t>(bytes[0]) |
		(static_cast<std::uint32_t>(bytes[1]) << 8) |
		(static_cast<std::uint32_t>(bytes[2]) << 16) |
		(static_cast<std::uint32_t>(bytes[3]) << 24);
	return true;
}

std::string ShaderLog(unsigned int shader)
{
	int length = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
	if (length <= 1)
		return {};
	std::string text(static_cast<std::size_t>(length), '\0');
	glGetShaderInfoLog(shader, length, nullptr, text.data());
	return text;
}

std::string ProgramLog(unsigned int program)
{
	int length = 0;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
	if (length <= 1)
		return {};
	std::string text(static_cast<std::size_t>(length), '\0');
	glGetProgramInfoLog(program, length, nullptr, text.data());
	return text;
}

unsigned int CompileShader(unsigned int type, const char* source, std::string* error)
{
	const unsigned int shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	int compiled = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled) {
		if (error) *error = ShaderLog(shader);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

bool CheckIndex(const json& values, std::size_t index)
{
	return values.is_array() && index < values.size();
}

bool AccessorView(const json& document, std::size_t accessorIndex,
	const std::vector<unsigned char>& binary, const unsigned char*& data,
	std::size_t& stride, std::size_t& count, std::uint32_t& componentType,
	std::string* error)
{
	const auto& accessors = document.at("accessors");
	const auto& views = document.at("bufferViews");
	if (!CheckIndex(accessors, accessorIndex)) {
		if (error) *error = "Fox model references an invalid accessor.";
		return false;
	}
	const json& accessor = accessors[accessorIndex];
	if (accessor.contains("sparse") || !accessor.contains("bufferView")) {
		if (error) *error = "Fox model uses an unsupported sparse accessor.";
		return false;
	}
	const std::size_t viewIndex = accessor.at("bufferView").get<std::size_t>();
	if (!CheckIndex(views, viewIndex)) {
		if (error) *error = "Fox model references an invalid buffer view.";
		return false;
	}
	const json& view = views[viewIndex];
	if (view.value("buffer", 0) != 0) {
		if (error) *error = "Fox model uses an unsupported external buffer.";
		return false;
	}
	componentType = accessor.at("componentType").get<std::uint32_t>();
	count = accessor.at("count").get<std::size_t>();
	const std::size_t componentSize = componentType == 5126 || componentType == 5125 ? 4
		: componentType == 5123 ? 2 : componentType == 5121 ? 1 : 0;
	if (componentSize == 0) {
		if (error) *error = "Fox model uses an unsupported accessor component type.";
		return false;
	}
	const std::size_t componentCount = accessor.at("type") == "VEC3" ? 3
		: accessor.at("type") == "VEC2" ? 2
		: accessor.at("type") == "SCALAR" ? 1 : 0;
	if (componentCount == 0 || (accessor.at("type") == "VEC3" && componentType != 5126)) {
		if (error) *error = "Fox model uses an unsupported vertex attribute format.";
		return false;
	}
	const std::size_t elementSize = componentSize * componentCount;
	const std::size_t viewOffset = view.value("byteOffset", std::size_t{ 0 });
	const std::size_t accessorOffset = accessor.value("byteOffset", std::size_t{ 0 });
	if (viewOffset > binary.size() || accessorOffset > binary.size() - viewOffset) {
		if (error) *error = "Fox model accessor points outside its binary data.";
		return false;
	}
	const std::size_t offset = viewOffset + accessorOffset;
	stride = view.value("byteStride", elementSize);
	if (stride < elementSize || (count > 0 &&
		(count - 1 > (std::numeric_limits<std::size_t>::max() - offset - elementSize) / stride ||
		 offset + (count - 1) * stride + elementSize > binary.size()))) {
		if (error) *error = "Fox model accessor exceeds its binary data.";
		return false;
	}
	data = binary.data() + offset;
	return true;
}

float ReadFloat(const unsigned char* source)
{
	float value = 0.0f;
	std::memcpy(&value, source, sizeof(value));
	return value;
}

bool ReadIndex(const unsigned char* source, std::uint32_t componentType, std::uint32_t& value)
{
	if (componentType == 5121) {
		value = source[0];
		return true;
	}
	if (componentType == 5123) {
		value = static_cast<std::uint32_t>(source[0]) |
			(static_cast<std::uint32_t>(source[1]) << 8);
		return true;
	}
	if (componentType == 5125) {
		value = static_cast<std::uint32_t>(source[0]) |
			(static_cast<std::uint32_t>(source[1]) << 8) |
			(static_cast<std::uint32_t>(source[2]) << 16) |
			(static_cast<std::uint32_t>(source[3]) << 24);
		return true;
	}
	return false;
}

} // namespace

FoxModel::~FoxModel()
{
	Shutdown();
}

bool FoxModel::Load(const std::string& path, std::string* error)
{
	Shutdown();
	if (error)
		error->clear();

	try {
		std::ifstream file(path, std::ios::binary);
		if (!file) {
			if (error) *error = "Could not open fox model: " + path;
			return false;
		}
		std::uint32_t magic = 0, version = 0, totalLength = 0;
		if (!ReadU32(file, magic) || !ReadU32(file, version) || !ReadU32(file, totalLength) ||
			magic != kGlbMagic || version != 2) {
			if (error) *error = "Fox model is not a valid glTF binary (GLB 2.0) file.";
			return false;
		}
		file.seekg(0, std::ios::end);
		const std::streamoff fileSize = file.tellg();
		if (fileSize < 20 || totalLength > static_cast<std::uint64_t>(fileSize)) {
			if (error) *error = "Fox model has an invalid GLB length.";
			return false;
		}
		file.seekg(12, std::ios::beg);
		json document;
		std::vector<unsigned char> binary;
		while (file.tellg() >= 0 && file.tellg() < totalLength) {
			std::uint32_t chunkLength = 0, chunkType = 0;
			if (!ReadU32(file, chunkLength) || !ReadU32(file, chunkType) ||
				chunkLength > totalLength - static_cast<std::uint64_t>(file.tellg())) {
				if (error) *error = "Fox model contains an invalid GLB chunk.";
				return false;
			}
			if (chunkType == kJsonChunk) {
				std::string source(chunkLength, '\0');
				if (!file.read(source.data(), chunkLength)) {
					if (error) *error = "Could not read fox model metadata.";
					return false;
				}
				document = json::parse(source);
			} else if (chunkType == kBinChunk) {
				binary.resize(chunkLength);
				if (!file.read(reinterpret_cast<char*>(binary.data()), chunkLength)) {
					if (error) *error = "Could not read fox model geometry.";
					return false;
				}
			} else {
				file.seekg(chunkLength, std::ios::cur);
			}
		}
		if (!document.is_object() || binary.empty() || !document.contains("nodes") ||
			!document.contains("meshes") || !document.contains("accessors") ||
			!document.contains("bufferViews")) {
			if (error) *error = "Fox GLB is missing its scene or geometry.";
			return false;
		}

		const char* vertexSource =
			"#version 130\n"
			"in vec3 aPosition; in vec3 aNormal;\n"
			"uniform mat4 uMvp; uniform mat4 uModel;\n"
			"out vec3 vNormal;\n"
			"void main(){ gl_Position=uMvp*vec4(aPosition,1.0); vNormal=mat3(uModel)*aNormal; }\n";
		const char* fragmentSource =
			"#version 130\n"
			"uniform vec4 uColor; in vec3 vNormal; out vec4 fragColor;\n"
			"void main(){ vec3 n=normalize(vNormal); vec3 l=normalize(vec3(-0.35,0.75,0.55));"
			"float d=max(dot(n,l),0.0); float light=0.38+0.62*d;"
			"fragColor=vec4(uColor.rgb*light,uColor.a); }\n";
		std::string shaderError;
		const unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource, &shaderError);
		const unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource, &shaderError);
		if (!vertexShader || !fragmentShader) {
			if (vertexShader) glDeleteShader(vertexShader);
			if (fragmentShader) glDeleteShader(fragmentShader);
			if (error) *error = "Could not compile fox model shader: " + shaderError;
			return false;
		}
		m_program = glCreateProgram();
		glAttachShader(m_program, vertexShader);
		glAttachShader(m_program, fragmentShader);
		glBindAttribLocation(m_program, 0, "aPosition");
		glBindAttribLocation(m_program, 1, "aNormal");
		glLinkProgram(m_program);
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);
		int linked = GL_FALSE;
		glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
		if (!linked) {
			if (error) *error = "Could not link fox model shader: " + ProgramLog(m_program);
			Shutdown();
			return false;
		}
		m_modelViewProjectionLocation = glGetUniformLocation(m_program, "uMvp");
		m_modelLocation = glGetUniformLocation(m_program, "uModel");
		m_colorLocation = glGetUniformLocation(m_program, "uColor");

		std::vector<std::size_t> meshPartOffsets;
		const auto& meshes = document.at("meshes");
		const auto& materials = document.value("materials", json::array());
		for (const json& mesh : meshes) {
			meshPartOffsets.push_back(m_parts.size());
			for (const json& primitive : mesh.at("primitives")) {
				if (primitive.value("mode", 4) != 4 || !primitive.contains("attributes") ||
					!primitive["attributes"].contains("POSITION")) {
					if (error) *error = "Fox model contains a primitive format the launcher cannot render.";
					Shutdown();
					return false;
				}
				const std::size_t positionAccessor = primitive["attributes"]["POSITION"].get<std::size_t>();
				const unsigned char* positions = nullptr;
				std::size_t positionStride = 0, vertexCount = 0;
				std::uint32_t positionType = 0;
				if (!AccessorView(document, positionAccessor, binary, positions, positionStride,
						vertexCount, positionType, error))
					return false;
				if (positionType != 5126 || document.at("accessors")[positionAccessor].at("type") != "VEC3") {
					if (error) *error = "Fox model positions must be 32-bit vectors.";
					return false;
				}
				std::vector<Vertex> vertices(vertexCount);
				const bool hasNormals = primitive["attributes"].contains("NORMAL");
				const unsigned char* normals = nullptr;
				std::size_t normalStride = 0, normalCount = 0;
				std::uint32_t normalType = 0;
				if (hasNormals && !AccessorView(document,
						primitive["attributes"]["NORMAL"].get<std::size_t>(), binary, normals,
						normalStride, normalCount, normalType, error))
					return false;
				if (hasNormals && (normalCount != vertexCount || normalType != 5126 ||
						document.at("accessors")[primitive["attributes"]["NORMAL"].get<std::size_t>()].at("type") != "VEC3")) {
					if (error) *error = "Fox model has an invalid normal attribute.";
					return false;
				}
				for (std::size_t i = 0; i < vertexCount; ++i) {
					for (int component = 0; component < 3; ++component) {
						vertices[i].position[component] = ReadFloat(positions + i * positionStride + component * sizeof(float));
						vertices[i].normal[component] = hasNormals
							? ReadFloat(normals + i * normalStride + component * sizeof(float))
							: 0.0f;
					}
					if (!hasNormals)
						vertices[i].normal[1] = 1.0f;
				}

				Part part;
				std::vector<std::uint32_t> indices;
				if (primitive.contains("indices")) {
					const unsigned char* indexData = nullptr;
					std::size_t indexStride = 0, indexCount = 0;
					std::uint32_t indexComponent = 0;
					if (!AccessorView(document, primitive["indices"].get<std::size_t>(), binary,
							indexData, indexStride, indexCount, indexComponent, error))
						return false;
					if (document.at("accessors")[primitive["indices"].get<std::size_t>()].at("type") != "SCALAR" ||
						(indexComponent != 5121 && indexComponent != 5123 && indexComponent != 5125)) {
						if (error) *error = "Fox model uses an unsupported index format.";
						return false;
					}
					indices.resize(indexCount);
					for (std::size_t i = 0; i < indexCount; ++i) {
						if (!ReadIndex(indexData + i * indexStride, indexComponent, indices[i]) ||
							indices[i] >= vertexCount) {
							if (error) *error = "Fox model contains an invalid mesh index.";
							return false;
						}
					}
					part.indexType = indexComponent == 5121 ? GL_UNSIGNED_BYTE
						: indexComponent == 5123 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
				} else {
					indices.resize(vertexCount);
					for (std::size_t i = 0; i < vertexCount; ++i)
						indices[i] = static_cast<std::uint32_t>(i);
					part.indexType = GL_UNSIGNED_INT;
				}
				part.indexCount = static_cast<int>(indices.size());
				if (primitive.contains("material")) {
					const std::size_t materialIndex = primitive["material"].get<std::size_t>();
					if (CheckIndex(materials, materialIndex) &&
						materials[materialIndex].contains("pbrMetallicRoughness") &&
						materials[materialIndex]["pbrMetallicRoughness"].contains("baseColorFactor")) {
						for (std::size_t i = 0; i < part.color.size(); ++i)
							part.color[i] = materials[materialIndex]["pbrMetallicRoughness"]["baseColorFactor"][i].get<float>();
					}
				}

				glGenVertexArrays(1, &part.vertexArray);
				glBindVertexArray(part.vertexArray);
				glGenBuffers(1, &part.vertexBuffer);
				glBindBuffer(GL_ARRAY_BUFFER, part.vertexBuffer);
				glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
					vertices.data(), GL_STATIC_DRAW);
				glGenBuffers(1, &part.indexBuffer);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.indexBuffer);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
					indices.data(), GL_STATIC_DRAW);
				glEnableVertexAttribArray(0);
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
					reinterpret_cast<void*>(offsetof(Vertex, position)));
				glEnableVertexAttribArray(1);
				glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
					reinterpret_cast<void*>(offsetof(Vertex, normal)));
				glBindVertexArray(0);
				m_parts.push_back(part);
			}
		}

		const auto& nodes = document.at("nodes");
		m_nodeMatrices.resize(nodes.size(), Identity());
		std::vector<bool> visited(nodes.size(), false);
		std::array<float, 3> minimum{
			std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()
		};
		std::array<float, 3> maximum{
			std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()
		};
		std::vector<std::size_t> roots;
		const auto& scenes = document.value("scenes", json::array());
		const std::size_t sceneIndex = document.value("scene", std::size_t{ 0 });
		if (CheckIndex(scenes, sceneIndex) && scenes[sceneIndex].contains("nodes"))
			for (const auto& node : scenes[sceneIndex]["nodes"])
				roots.push_back(node.get<std::size_t>());
		else
			for (std::size_t i = 0; i < nodes.size(); ++i)
				roots.push_back(i);

		std::function<bool(std::size_t, const Mat4&)> visit =
			[&](std::size_t nodeIndex, const Mat4& parent) {
				if (!CheckIndex(nodes, nodeIndex) || visited[nodeIndex]) {
					if (error) *error = "Fox model contains an invalid or cyclic scene hierarchy.";
					return false;
				}
				visited[nodeIndex] = true;
				const json& node = nodes[nodeIndex];
				const Mat4 local = NodeTransform(node);
				const Mat4 world = Multiply(parent, local);
				m_nodeMatrices[nodeIndex] = world;
				if (node.contains("name") && node["name"] == "Cube_0")
					m_headNode = nodeIndex;
				if (node.contains("mesh")) {
					const std::size_t meshIndex = node["mesh"].get<std::size_t>();
					if (!CheckIndex(meshes, meshIndex)) {
						if (error) *error = "Fox model node references an invalid mesh.";
						return false;
					}
					std::size_t partIndex = meshPartOffsets[meshIndex];
					for (const json& primitive : meshes[meshIndex]["primitives"]) {
						const Part& part = m_parts.at(partIndex);
						const std::size_t accessorIndex = primitive["attributes"]["POSITION"].get<std::size_t>();
						const unsigned char* positions = nullptr;
						std::size_t stride = 0, count = 0;
						std::uint32_t componentType = 0;
						if (!AccessorView(document, accessorIndex, binary, positions, stride, count, componentType, error))
							return false;
						for (std::size_t i = 0; i < count; ++i) {
							const std::array<float, 3> point{
								ReadFloat(positions + i * stride),
								ReadFloat(positions + i * stride + sizeof(float)),
								ReadFloat(positions + i * stride + sizeof(float) * 2)
							};
							const auto transformed = TransformPosition(world, point);
							for (int axis = 0; axis < 3; ++axis) {
								minimum[axis] = std::min(minimum[axis], transformed[axis]);
								maximum[axis] = std::max(maximum[axis], transformed[axis]);
							}
						}
						m_drawParts.push_back({ partIndex++, nodeIndex });
					}
				}
				if (node.contains("children"))
					for (const auto& child : node["children"])
						if (!visit(child.get<std::size_t>(), world))
							return false;
				return true;
			};
		for (const std::size_t root : roots)
			if (!visit(root, Identity())) {
				Shutdown();
				return false;
			}
		if (m_drawParts.empty()) {
			if (error) *error = "Fox model scene has no renderable meshes.";
			Shutdown();
			return false;
		}
		for (int axis = 0; axis < 3; ++axis)
			m_center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
		const float largestExtent = std::max({ maximum[0] - minimum[0],
			maximum[1] - minimum[1], maximum[2] - minimum[2] });
		if (largestExtent <= 0.0f) {
			if (error) *error = "Fox model has no measurable geometry.";
			Shutdown();
			return false;
		}
		m_scale = 2.4f / largestExtent;
		if (!CreateRenderTarget(error)) {
			Shutdown();
			return false;
		}
		m_loaded = true;
		return true;
	}
	catch (const std::exception& exception) {
		Shutdown();
		if (error) *error = std::string("Could not load fox model: ") + exception.what();
		return false;
	}
}

void FoxModel::SetReduceMotion(bool reduce)
{
	m_reduceMotion = reduce;
	if (reduce)
		m_hopTime = -1.0f;
}

bool FoxModel::CreateRenderTarget(std::string* error)
{
	GLint previousFramebuffer = 0;
	GLint previousTexture = 0;
	GLint previousRenderbuffer = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);

	m_renderSize = kRenderSize;
	glGenTextures(1, &m_colorTexture);
	glBindTexture(GL_TEXTURE_2D, m_colorTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_renderSize, m_renderSize, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenRenderbuffers(1, &m_depthBuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_renderSize, m_renderSize);
	glGenFramebuffers(1, &m_framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);
	const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
	glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
	if (!complete) {
		if (error) *error = "Could not create the transparent fox render target.";
		return false;
	}
	return true;
}

void FoxModel::Draw(float size)
{
	const ImVec2 itemSize(size, size);
	const ImVec2 minimum = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##fox_mascot", itemSize);
	const bool hovered = ImGui::IsItemHovered();
	const ImGuiIO& io = ImGui::GetIO();
	const float cursorX = (io.MousePos.x - (minimum.x + size * 0.5f)) / (size * 4.0f);
	const float cursorY = (io.MousePos.y - (minimum.y + size * 0.5f)) / (size * 4.0f);
	m_targetBodyYaw = std::clamp(cursorX * 0.55f, -0.55f, 0.55f);
	m_targetBodyPitch = std::clamp(-cursorY * 0.32f, -0.32f, 0.32f);
	m_targetHeadYaw = std::clamp(cursorX * 0.7f, -0.6f, 0.6f);
	m_targetHeadPitch = std::clamp(-cursorY * 0.5f, -0.45f, 0.45f);
	if (hovered && !m_reduceMotion && ImGui::IsItemClicked(ImGuiMouseButton_Left))
		m_hopTime = 0.0f;

	std::string error;
	if (!m_loaded || !Render(&error)) {
		const ImVec2 maximum(minimum.x + size, minimum.y + size);
		ImGui::GetWindowDrawList()->AddRectFilled(minimum, maximum,
			ImGui::GetColorU32(Theme::C().cardBg), Theme::S(6.0f));
		if (!m_loadError.empty())
			ImGui::GetWindowDrawList()->AddText(ImVec2(minimum.x, maximum.y + Theme::S(2.0f)),
				ImGui::GetColorU32(Theme::C().error), "Fox model unavailable");
		ImGui::Dummy(ImVec2(0.0f, 0.0f));
		return;
	}
	const ImVec2 maximum(minimum.x + size, minimum.y + size);
	ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(m_colorTexture),
		minimum, maximum, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
}

bool FoxModel::Render(std::string* error)
{
	if (!m_loaded || !m_framebuffer)
		return false;
	const float delta = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
	const float easing = m_reduceMotion ? 1.0f : 1.0f - std::exp(-delta * 12.0f);
	m_headYaw += (m_targetHeadYaw - m_headYaw) * easing;
	m_headPitch += (m_targetHeadPitch - m_headPitch) * easing;
	m_bodyYaw += (m_targetBodyYaw - m_bodyYaw) * easing;
	m_bodyPitch += (m_targetBodyPitch - m_bodyPitch) * easing;
	if (m_hopTime >= 0.0f) {
		m_hopTime += delta;
		if (m_hopTime >= 0.48f)
			m_hopTime = -1.0f;
	}
	const float hop = m_hopTime < 0.0f ? 0.0f
		: std::sin(m_hopTime / 0.48f * kPi) * 0.3f;

	GLint previousFramebuffer = 0, previousProgram = 0, previousVertexArray = 0;
	GLint previousArrayBuffer = 0, previousRenderbuffer = 0, previousActiveTexture = 0;
	GLint previousTexture = 0, previousViewport[4]{}, previousDepthFunction = GL_LESS;
	GLfloat previousClearColor[4]{};
	const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
	const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
	const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
	glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
	glGetIntegerv(GL_VIEWPORT, previousViewport);
	glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunction);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

	glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
	glViewport(0, 0, m_renderSize, m_renderSize);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	const Mat4 projection = Perspective(38.0f * kPi / 180.0f, 1.0f, 0.1f, 30.0f);
	const Mat4 view = LookAt(0.0f, 1.0f, 5.0f, 0.0f, 0.0f, 0.0f);
	const Mat4 orientation = Multiply(RotationY(-0.65f + m_bodyYaw), RotationX(m_bodyPitch));
	const Mat4 modelRoot = Multiply(orientation,
		Multiply(Scaling(m_scale, m_scale, m_scale),
			Translation(-m_center[0], -m_center[1] + hop, -m_center[2])));
	glUseProgram(m_program);
	for (const DrawPart& drawPart : m_drawParts) {
		const Part& part = m_parts[drawPart.part];
		Mat4 node = m_nodeMatrices[drawPart.node];
		if (drawPart.node == m_headNode) {
			const Mat4 pivot = Translation(node[12], node[13], node[14]);
			const Mat4 inversePivot = Translation(-node[12], -node[13], -node[14]);
			const Mat4 aim = Multiply(RotationY(m_headYaw), RotationZ(m_headPitch));
			node = Multiply(pivot, Multiply(aim, Multiply(inversePivot, node)));
		}
		const Mat4 model = Multiply(modelRoot, node);
		const Mat4 mvp = Multiply(projection, Multiply(view, model));
		glUniformMatrix4fv(m_modelViewProjectionLocation, 1, GL_FALSE, mvp.data());
		glUniformMatrix4fv(m_modelLocation, 1, GL_FALSE, model.data());
		glUniform4fv(m_colorLocation, 1, part.color.data());
		glBindVertexArray(part.vertexArray);
		glDrawElements(GL_TRIANGLES, part.indexCount, part.indexType, nullptr);
	}
	glBindVertexArray(static_cast<GLuint>(previousVertexArray));
	glUseProgram(static_cast<GLuint>(previousProgram));
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
	glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
	glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
	glDepthFunc(static_cast<GLenum>(previousDepthFunction));
	if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
	glActiveTexture(static_cast<GLenum>(previousActiveTexture));
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
	glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
	(void)error;
	return true;
}

void FoxModel::DestroyRenderTarget()
{
	if (m_framebuffer) glDeleteFramebuffers(1, &m_framebuffer);
	if (m_depthBuffer) glDeleteRenderbuffers(1, &m_depthBuffer);
	if (m_colorTexture) glDeleteTextures(1, &m_colorTexture);
	m_framebuffer = 0;
	m_depthBuffer = 0;
	m_colorTexture = 0;
	m_renderSize = 0;
}

void FoxModel::Shutdown()
{
	DestroyRenderTarget();
	for (Part& part : m_parts) {
		if (part.vertexArray) glDeleteVertexArrays(1, &part.vertexArray);
		if (part.vertexBuffer) glDeleteBuffers(1, &part.vertexBuffer);
		if (part.indexBuffer) glDeleteBuffers(1, &part.indexBuffer);
	}
	m_parts.clear();
	m_drawParts.clear();
	m_nodeMatrices.clear();
	if (m_program)
		glDeleteProgram(m_program);
	m_program = 0;
	m_loaded = false;
	m_headNode = static_cast<std::size_t>(-1);
}

#pragma once

#include <glm/glm.hpp>
#include <vector>

struct SubMesh;

struct Vertex
{
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 color = glm::vec3(1.0f);
	glm::vec3 normal = glm::vec3(0.0f);
	glm::vec2 uv = glm::vec2(0.0f);
};

struct Mesh
{
	std::string name;
	std::vector<SubMesh> subMeshes;
};

struct SubMesh
{
	size_t vertexStart = 0;
	size_t vertexCount = 0;
	size_t indexStart = 0;
	size_t indexCount = 0;
	uint32_t materialId = 0;
};

struct Image
{
	int width;
	int height;
	int channels;
	unsigned char *data;
};


enum class CameraType
{
	orbit, firstPerson
};

struct Camera
{
	CameraType type = CameraType::firstPerson;
	float fovY = 65.0f;
	float nearPlane = 0.01f;
	float farPlane = 1000.0f;
	glm::vec3 forward = glm::vec3(0, 0, -1);
	glm::vec3 right = glm::vec3(1, 0, 0);
	glm::vec3 up = glm::vec3(0, 1, 0);
	float distance = 3;
	float yaw = 0;
	float pitch = 0;
};

struct Material
{
	glm::vec4 baseColor = glm::vec4(1, 1, 1, 1);
	uint32_t textureIndex = 0;
};

struct Texture
{
	uint32_t imageId = 0;
	uint32_t samplerId = 0;
};

enum class LightType { point, spot };

struct Light
{
	LightType type = LightType::point;
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 color = glm::vec3(1.0f);
	glm::vec3 direction = glm::vec3(0, 0, -1);
	float intensity = 1.0f;
	float range = 10.0f;
	float innerConeAngle = 0;
	float outerConeAngle = glm::quarter_pi<float>();
};

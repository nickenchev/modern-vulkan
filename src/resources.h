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

struct Camera
{
	float fovY = 65.0f;
	float nearPlane = 0.01f;
	float farPlane = 1000.0f;
};
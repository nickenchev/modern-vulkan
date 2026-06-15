#pragma once

#include <glm/glm.hpp>
#include <vector>

struct SubMesh;

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 color;
	glm::vec2 uv;
};

struct Mesh
{
	uint32_t vertexBufferId = 0;
	uint32_t indexBufferId = 0;
	std::string name;
	std::vector<SubMesh> subMeshes;
};

struct SubMesh
{
	size_t vertexStart;
	size_t vertexCount;
	size_t indexStart;
	size_t indexCount;
	uint32_t materialId;
};

struct Image
{
	int width;
	int height;
	int channels;
	unsigned char *data;
};

struct Material
{
	glm::vec4 baseColor;
	uint32_t baseColorTextureIndex;
};

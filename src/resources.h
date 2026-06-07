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
	std::string name;
	std::vector<SubMesh> subMeshes;
};

struct SubMesh
{
	size_t vertexStart;
	size_t vertexCount;
	size_t indexStart;
	size_t indexCount;
};

struct Model
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

struct Image
{
	int width;
	int height;
	int channels;
};

struct Material
{
	glm::vec3 baseColor;
};

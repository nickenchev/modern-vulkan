#pragma once

#include <tiny_gltf_v3.h>
#include <memory>
#include <string>

#include "resources.h"

struct ImportedResources
{
	bool success = false;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<Material> materials;
	std::vector<Mesh> meshes;
};

void importModel(const std::string &filePath);

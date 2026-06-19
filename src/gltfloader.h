#pragma once

#include <tiny_gltf_v3.h>
#include <memory>
#include <string>

#include "resources.h"

class NodeWorld;

struct ImportedResources
{
	bool success = false;
	std::vector<Image> images;
	std::vector<Material> materials;
	std::vector<Mesh> meshes;
};

bool parseModel(const std::string &filePath, tg3_model &model);
bool importResources(const std::string &filePath, const tg3_model &model, std::vector<Vertex> &vertices,
	std::vector<uint32_t> &indices, size_t vertBufferOffset, size_t idxBufferOffset, ImportedResources &resources);
uint32_t importNode(NodeWorld &nodeWorld, const tg3_model &model, int32_t nodeIndex, uint32_t parentId, uint32_t prevSiblingId, std::vector<uint32_t> &meshIds);

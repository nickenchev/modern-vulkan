#pragma once

#include <vector>
#include <glm/glm.hpp>

class Node
{
	glm::vec3 position;
	glm::vec3 scale;
	glm::mat4 localTransform;
	bool dirty = true;

public:
	uint32_t meshId;
	uint32_t parentId = 0;
	uint32_t nextSiblingId = 0;
	uint32_t firstChildId = 0;

	glm::vec3 getPosition() const { return position; }
	void setPosition(glm::vec3 &position)
	{
		this->position = position;
		this->dirty = true;
	}

	glm::vec4 getLocalMatrix()
	{
		if (dirty)
		{
			// recalculate the local transform matrix
			localTransform = glm::mat4(1);
			dirty = false;
		}
	}
};

class NodeWorld
{
	constexpr static size_t MAX_NODES = 256;

	std::vector<Node> nodes;

public:
	NodeWorld()
	{
		nodes.reserve(MAX_NODES);
	}

	std::pair<Node &, uint32_t> createNode()
	{
		assert(nodes.size() < MAX_NODES && "Node world is at capacity");

		nodes.push_back(Node{});
		uint32_t nodeId = nodes.size();
		return { nodes[nodeId - 1], nodeId };
	}

	Node &getNode(uint32_t nodeId)
	{
		assert(nodeId > 0 && "Tried retrieving a node with nil ID");
		return nodes[nodeId - 1];
	}

	const std::vector<Node> &allNodes() const { return nodes; }
};
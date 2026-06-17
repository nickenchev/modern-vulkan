#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Node
{
	glm::vec3 translation = glm::vec3(0, 0, 0);
	glm::vec3 scale = glm::vec3(1, 1, 1);
	glm::mat4 transform = glm::mat4(1);
	glm::quat rotation = glm::quat(1, 0, 0, 0);
	bool dirty = true;

public:
	uint32_t meshId = 0;
	uint32_t parentId = 0;
	uint32_t nextSiblingId = 0;
	uint32_t firstChildId = 0;

	glm::vec3 getTranslation() const { return translation; }
	void setTranslation(const glm::vec3 &translation)
	{
		this->translation = translation;
		dirty = true;
	}

	glm::quat getRotation() const { return rotation; }
	void setRotation(const glm::quat &rotation)
	{
		this->rotation = rotation;
		dirty = true;
	}

	glm::vec3 getScale() const { return scale; }
	void setScale(const glm::vec3 &scale)
	{
		this->scale = scale;
		dirty = true;
	}

	glm::mat4 getTransform()
	{
		if (dirty)
		{
			// recalculate the local transform matrix
			glm::mat4 matTranslate = glm::translate(glm::mat4(1), translation);
			glm::mat4 matRotate = glm::mat4_cast(rotation);
			glm::mat4 matScale = glm::scale(glm::mat4(1), scale);
			transform = matTranslate * matRotate * matScale;
			dirty = false;
		}
		return transform;
	}
	void setTransform(glm::mat4 &transform)
	{
		this->transform = transform;
		dirty = false;
	}
};

class NodeWorld
{
	constexpr static size_t MAX_NODES = 1024;

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

	std::vector<Node> &allNodes() { return nodes; }
};
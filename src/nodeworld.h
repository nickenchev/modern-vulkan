#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Node
{
	glm::vec3 m_translation = glm::vec3(0, 0, 0);
	glm::vec3 m_scale = glm::vec3(1, 1, 1);
	glm::mat4 m_transform = glm::mat4(1);
	glm::quat m_rotation = glm::quat(1, 0, 0, 0);
	bool m_dirty = true;

public:
	uint32_t meshId = 0;
	uint32_t parentId = 0;
	uint32_t nextSiblingId = 0;
	uint32_t firstChildId = 0;

	glm::vec3 getTranslation() const { return m_translation; }
	void setTranslation(const glm::vec3 &translation)
	{
		this->m_translation = translation;
		m_dirty = true;
	}

	glm::quat getRotation() const { return m_rotation; }
	void setRotation(const glm::quat &rotation)
	{
		this->m_rotation = rotation;
		m_dirty = true;
	}

	glm::vec3 getScale() const { return m_scale; }
	void setScale(const glm::vec3 &scale)
	{
		this->m_scale = scale;
		m_dirty = true;
	}

	glm::mat4 getTransform()
	{
		if (m_dirty)
		{
			// recalculate the local transform matrix
			glm::mat4 matTranslate = glm::translate(glm::mat4(1), m_translation);
			glm::mat4 matRotate = glm::mat4_cast(m_rotation);
			glm::mat4 matScale = glm::scale(glm::mat4(1), m_scale);
			m_transform = matTranslate * matRotate * matScale;
			m_dirty = false;
		}
		return m_transform;
	}
	void setTransform(glm::mat4 &transform)
	{
		m_transform = transform;
		m_dirty = false;
	}
};

class NodeWorld
{
	std::vector<Node> m_nodes;
	size_t m_maxNodes = 0;

public:
	void initialize(const size_t maxNodes)
	{
		m_maxNodes = maxNodes;
		m_nodes.reserve(m_maxNodes); // TODO: Use a paged pool perhaps
	}

	size_t maxNodes() const { return m_maxNodes; }

	std::pair<Node &, uint32_t> createNode()
	{
		assert(m_nodes.size() < m_maxNodes && "Node world is at capacity");
		m_nodes.push_back(Node{});
		uint32_t nodeId = m_nodes.size();
		return { m_nodes[nodeId - 1], nodeId };
	}

	Node &getNode(uint32_t nodeId)
	{
		assert(nodeId > 0 && "Tried retrieving a node with nil ID");
		return m_nodes[nodeId - 1];
	}

	std::vector<Node> &allNodes() { return m_nodes; }
};
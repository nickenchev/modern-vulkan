#include "gltfloader.h"

#include <print>
#include <filesystem>
#include <glm/glm.hpp>
#include <stb_image.h>

#include "resources.h"
#include "nodeworld.h"

#include <glm/gtc/type_ptr.inl>

bool parseModel(const std::string &filePath, tg3_model &model)
{
	std::filesystem::path path(filePath);

	// open the gltf file
	tg3_parse_options opts;
	tg3_error_stack errors;

	if (!std::filesystem::exists(path))
	{
		std::print("File doesn't exist: {}\n", filePath);
		return false;
	}

	tg3_parse_options_init(&opts);
	tg3_error_stack_init(&errors);
	tg3_error_code parseResult = tg3_parse_file(&model, &errors, filePath.c_str(), filePath.size(), &opts);
	if (parseResult != TG3_OK)
	{
		std::print("Error parsing glTF file, errors found:\n");
		for (int i = 0; i < errors.count; ++i)
		{
			std::print("{}\n", errors.entries[i].message);
		}
		return false;
	}
	tg3_error_stack_free(&errors);
	return true;
}

bool importResources(const std::string &filePath, const tg3_model &model, ImportedResources &resources)
{
	std::filesystem::path path(filePath);

	// import source textures
	resources.images.resize(model.images_count);
	for (int i = 0; i < model.images_count; ++i)
	{
		Image &img = resources.images[i];
		std::filesystem::path imagePath = path.parent_path() / model.images[i].uri.data;
		std::print("Loading image {}/{}: {}\n", i + 1, model.images_count, model.images[i].uri.data);
		img.data = stbi_load(imagePath.string().c_str(), &img.width, &img.height, &img.channels, 4);
	}

	// import materials
	resources.materials.resize(model.materials_count);
	for (int i = 0; i < model.materials_count; ++i)
	{
		Material &mat = resources.materials[i];
		const tg3_material *tg3mat = &model.materials[i];
		mat.baseColor = glm::vec4(
			tg3mat->pbr_metallic_roughness.base_color_factor[0],
			tg3mat->pbr_metallic_roughness.base_color_factor[1],
			tg3mat->pbr_metallic_roughness.base_color_factor[2],
			tg3mat->pbr_metallic_roughness.base_color_factor[3]);

		// check for a albedo texture map
		if (tg3mat->pbr_metallic_roughness.base_color_texture.index != -1)
		{
			const tg3_texture *tex = &model.textures[tg3mat->pbr_metallic_roughness.base_color_texture.index];
			mat.baseColorTextureIndex = tex->source; // source is an index into the gltf images list
		}
	}

	// import meshes
	resources.meshes.resize(model.meshes_count);
	size_t totalVertices = 0;
	size_t totalIndices = 0;

	// total primitive count for geo buffers
	uint32_t totalPrimitives = 0;
	for (int i = 0; i < model.meshes_count; ++i)
	{
		totalPrimitives += model.meshes[i].primitives_count;
	}
	std::vector<std::pair<std::vector<Vertex>, std::vector<uint32_t>>> primitiveData(totalPrimitives);
	int primIdx = 0;

	// load all gltf mesh and primitive data
	for (int i = 0; i < model.meshes_count; ++i)
	{
		Mesh &mesh = resources.meshes[i];
		const tg3_mesh *tg3mesh = &model.meshes[i];
		mesh.name = tg3mesh->name.data != nullptr ? tg3mesh->name.data : "No Name";

		// start with vertex positions
		mesh.subMeshes.resize(tg3mesh->primitives_count);
		for (int j = 0; j < tg3mesh->primitives_count; ++j)
		{
			const tg3_primitive *primitive = &tg3mesh->primitives[j];
			std::vector<Vertex> &vertices = primitiveData[primIdx].first;
			std::vector<uint32_t> &indices = primitiveData[primIdx].second;
			primIdx++;

			// first look up the positions accessor to get vertex positions and total vertex count
			for (int k = 0; k < primitive->attributes_count; ++k)
			{
				const tg3_str_int_pair *attr = &primitive->attributes[k];
				if (strcmp(attr->key.data, "POSITION") == 0)
				{
					const tg3_accessor *accessor = &model.accessors[attr->value];
					const tg3_buffer_view *bufferView = &model.buffer_views[accessor->buffer_view];
					const tg3_buffer *buffer = &model.buffers[bufferView->buffer];

					if (accessor->type == TG3_TYPE_VEC3 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT)
					{
						vertices.resize(accessor->count);
						mesh.subMeshes[j].vertexStart = totalVertices;
						mesh.subMeshes[j].vertexCount = accessor->count;
						totalVertices += accessor->count;

						const float *positions = reinterpret_cast<const float *>(buffer->data.data + bufferView->byte_offset + accessor->byte_offset);
						for (uint64_t idx = 0; idx < accessor->count; ++idx)
						{
							vertices[idx].position = glm::vec3(positions[idx * 3], positions[idx * 3 + 1], positions[idx * 3 + 2]);
						}
					}
				}
			}

			// retrieve the rest of the per-vertex data
			for (int k = 0; k < primitive->attributes_count; ++k)
			{
				const tg3_str_int_pair *attr = &primitive->attributes[k];
				if (strcmp(attr->key.data, "NORMAL") == 0)
				{
					const tg3_accessor *accessor = &model.accessors[attr->value];
					const tg3_buffer_view *bufferView = &model.buffer_views[accessor->buffer_view];
					const tg3_buffer *buffer = &model.buffers[bufferView->buffer];

					if (accessor->type == TG3_TYPE_VEC3 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT)
					{
						const float *normals = reinterpret_cast<const float *>(buffer->data.data + bufferView->byte_offset + accessor->byte_offset);
						for (uint64_t idx = 0; idx < accessor->count; ++idx)
						{
							vertices[idx].normal = glm::vec3(normals[idx * 3], normals[idx * 3 + 1], normals[idx * 3 + 2]);
						}
					}
				}
				else if (strcmp(attr->key.data, "COLOR_0") == 0)
				{
					const tg3_accessor *accessor = &model.accessors[attr->value];
					const tg3_buffer_view *bufferView = &model.buffer_views[accessor->buffer_view];
					const tg3_buffer *buffer = &model.buffers[bufferView->buffer];

					if (accessor->type == TG3_TYPE_VEC3 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT)
					{
						const float *colors = reinterpret_cast<const float *>(buffer->data.data + bufferView->byte_offset + accessor->byte_offset);
						for (uint64_t idx = 0; idx < accessor->count; ++idx)
						{
							vertices[idx].color = glm::vec3(colors[idx * 3], colors[idx * 3 + 1], colors[idx * 3 + 2]);
						}
					}
				}
				else if (strcmp(attr->key.data, "TEXCOORD_0") == 0)
				{
					const tg3_accessor *accessor = &model.accessors[attr->value];
					const tg3_buffer_view *bufferView = &model.buffer_views[accessor->buffer_view];
					const tg3_buffer *buffer = &model.buffers[bufferView->buffer];

					if (accessor->type == TG3_TYPE_VEC2 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT)
					{
						const float *uvs = reinterpret_cast<const float *>(buffer->data.data + bufferView->byte_offset + accessor->byte_offset);
						for (uint64_t idx = 0; idx < accessor->count; ++idx)
						{
							vertices[idx].uv = glm::vec2(uvs[idx * 2], uvs[idx * 2 + 1]);
						}
					}
				}
			}

			// copy index data
			if (primitive->indices != -1 && vertices.size())
			{
				const tg3_accessor *accessor = &model.accessors[primitive->indices];
				const tg3_buffer_view *bufferView = &model.buffer_views[accessor->buffer_view];
				const tg3_buffer *buffer = &model.buffers[bufferView->buffer];
				mesh.subMeshes[j].indexStart = totalIndices;
				mesh.subMeshes[j].indexCount = accessor->count;
				totalIndices += accessor->count;
				indices.resize(accessor->count);

				if (accessor->component_type == TG3_COMPONENT_TYPE_UNSIGNED_INT)
				{
					const uint32_t *buffData = reinterpret_cast<const uint32_t *>(buffer->data.data + bufferView->byte_offset + accessor->byte_offset);
					memcpy(indices.data(), buffData, accessor->count * sizeof(uint32_t));
				}
				else if (accessor->component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT)
				{
					const uint16_t *buffData = reinterpret_cast<const uint16_t *>(buffer->data.data + bufferView->byte_offset + accessor->byte_offset);
					for (uint64_t idx = 0; idx < accessor->count; ++idx)
					{
						indices[idx] = static_cast<uint32_t>(buffData[idx]);
					}
				}
			}
		}
	}

	// flatten all the vertex data
	resources.vertices.resize(totalVertices);
	resources.indices.resize(totalIndices);

	size_t vertOffset = 0;
	size_t indexOffset = 0;
	for (auto &prim : primitiveData)
	{
		std::copy(prim.first.begin(), prim.first.end(), resources.vertices.begin() + vertOffset);
		std::copy(prim.second.begin(), prim.second.end(), resources.indices.begin() + indexOffset);
		vertOffset += prim.first.size();
		indexOffset += prim.second.size();
	}
	return true;
}

uint32_t importNode(NodeWorld &nodeWorld, const tg3_model &model, int32_t nodeIndex, uint32_t parentId, uint32_t prevSiblingId, std::vector<uint32_t> &meshIds)
{
	const tg3_node &tg3Node = model.nodes[nodeIndex];

	auto [node, nodeId] = nodeWorld.createNode();
	node.parentId = parentId;

	if (tg3Node.has_matrix)
	{
		glm::mat4 transform;
		float *transformPtr = glm::value_ptr(transform);
		for (int i = 0; i < 16; ++i)
		{
			transformPtr[i] = static_cast<float>(tg3Node.matrix[i]);
		}
		node.setTransform(transform);
	}
	else
	{
		glm::vec3 translation(tg3Node.translation[0], tg3Node.translation[1], tg3Node.translation[2]);
		glm::quat rotation(tg3Node.rotation[3], tg3Node.rotation[0], tg3Node.rotation[1], tg3Node.rotation[2]);
		glm::vec3 scale(tg3Node.scale[0], tg3Node.scale[1], tg3Node.scale[2]);
		
		node.setTranslation(translation);
		node.setRotation(rotation);
		node.setScale(scale);
	}

	if (tg3Node.mesh != -1)
	{
		node.meshId = meshIds[tg3Node.mesh];
	}

	if (prevSiblingId)
	{
		nodeWorld.getNode(prevSiblingId).nextSiblingId = nodeId;
	}

	// iterate through child nodes
	uint32_t lastChildId = 0;
	for (int i = 0; i < tg3Node.children_count; ++i)
	{
		int32_t childIndex = tg3Node.children[i];
		lastChildId = importNode(nodeWorld, model, childIndex, nodeId, lastChildId, meshIds);

		// set parent's first child id field
		if (!node.firstChildId)
		{
			node.firstChildId = lastChildId;
		}
	}

	return nodeId;
}

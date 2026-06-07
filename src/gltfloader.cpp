#include "gltfloader.h"

#include <print>
#include <filesystem>
#include <glm/glm.hpp>
#include <stb_image.h>
#include "resources.h"

void importModel(const std::string &filePath)
{
	std::filesystem::path path(filePath);

	// open the gltf file
	tg3_parse_options opts;
	tg3_error_stack errors;
	tg3_model model;

	if (!std::filesystem::exists(path))
	{
		std::print("File doesn't exist: {}\n", filePath);
		return;
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
		return;
	}
	tg3_error_stack_free(&errors);

	// import source textures
	std::vector<Image> images(model.images_count);
	for (int i = 0; i < model.images_count; ++i)
	{
		std::filesystem::path imagePath = path.parent_path() / model.images[i].uri.data;
		stbi_load(imagePath.string().c_str(), &images[i].width, &images[i].height, &images[i].channels, 4);
	}

	// import materials
	std::vector<Material> materials(model.materials_count);
	for (int i = 0; i < model.materials_count; ++i)
	{
		const tg3_material *mat = &model.materials[i];
		materials[i].baseColor = glm::vec4(
			mat->pbr_metallic_roughness.base_color_factor[0],
			mat->pbr_metallic_roughness.base_color_factor[1],
			mat->pbr_metallic_roughness.base_color_factor[2],
			mat->pbr_metallic_roughness.base_color_factor[3]);

		// check for a albedo texture map
		if (mat->pbr_metallic_roughness.base_color_texture.index != -1)
		{
			const tg3_texture *tex = &model.textures[mat->pbr_metallic_roughness.base_color_texture.index];
		}
	}

	// import meshes
	std::vector<Mesh> meshes(model.meshes_count);
	size_t totalVertices = 0;
	size_t totalIndices = 0;

	uint32_t totalPrimitives = 0;
	for (int i = 0; i < model.meshes_count; ++i)
	{
		totalPrimitives += model.meshes[i].primitives_count;
	}
	std::vector<std::pair<std::vector<Vertex>, std::vector<uint32_t>>> primitiveData(totalPrimitives);

	// load all gltf mesh and primitive data
	for (int i = 0; i < model.meshes_count; ++i)
	{
		const tg3_mesh *mesh = &model.meshes[i];
		meshes[i].name = mesh->name.data;

		// start with vertex positions
		meshes[i].subMeshes.resize(mesh->primitives_count);
		for (int j = 0; j < mesh->primitives_count; ++j)
		{
			const tg3_primitive *primitive = &mesh->primitives[j];
			std::vector<Vertex> &vertices = primitiveData[j].first;
			std::vector<uint32_t> &indices = primitiveData[j].second;

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
						meshes[i].subMeshes[j].vertexStart = totalVertices;
						meshes[i].subMeshes[j].vertexCount = accessor->count;
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
				meshes[i].subMeshes[j].indexStart = totalIndices;
				meshes[i].subMeshes[j].indexCount = accessor->count;
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
	std::vector<Vertex> allVertices(totalVertices);
	std::vector<uint32_t> allIndices(totalIndices);

	size_t vertOffset = 0;
	size_t indexOffset = 0;
	for (auto &prim : primitiveData)
	{
		std::copy(prim.first.begin(), prim.first.end(), allVertices.begin() + vertOffset);
		std::copy(prim.second.begin(), prim.second.end(), allIndices.begin() + indexOffset);
		vertOffset += prim.first.size();
		indexOffset += prim.second.size();
	}

	tg3_model_free(&model);
}

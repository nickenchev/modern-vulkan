#include "application.h"
#include "utils.h"

#include <SDL3/SDL.h>
#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <iostream>
#include <print>
#include <tiny_gltf_v3.h>
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_map>

void Application::showError(const std::string &errorMessasge) const
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessasge.c_str(), m_window);
}

bool Application::initialize()
{
	SDL_InitSubSystem(SDL_INIT_VIDEO);
	m_window = SDL_CreateWindow("Vulkan Learning", m_width, m_height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if (!m_window)
	{
		showError("Error creating window");
		return false;
	}
	SDL_SetWindowRelativeMouseMode(m_window, true);

	// setup renderer specifits here
	m_nodeWorld.initialize(1024); // maximum node-world size
	m_nodeRenderStack.reserve(128); // render-stack per-frame size

	if (!initializeVulkan())
	{
		return false;
	}
	return true;
}

bool Application::loadData()
{
	// preallocate mem for vertex and index data
	constexpr size_t vertexBufferBytes = 64 * 1024 * 1024; // 64MB vertex budget
	constexpr size_t indexBufferBytes = 32 * 1024 * 1024; // 32MB index budget
	constexpr size_t totalVerts = vertexBufferBytes / sizeof(Vertex);
	constexpr size_t totalIndices = indexBufferBytes / sizeof(uint32_t);
	m_vertices.resize(totalVerts);
	m_indices.resize(totalIndices);

	// fallback 1x1 white texture base color texture (0-index in tex array)
	uint32_t whitePixelData = 0xFFFFFFFF; // RGBA
	Image whitePixel
	{
		.width = 1,
		.height = 1,
		.channels = 4,
		.data = reinterpret_cast<unsigned char *>(&whitePixelData),
	};

	VkCommandBuffer whiteImgCmdBuff = startTransientCommandBuffer();
	auto [whiteImageId, whiteStagingBuffer] = createImage(whiteImgCmdBuff, whitePixel.data, whitePixel.width, whitePixel.height, 4);
	m_whitePixelImageId = whiteImageId;
	submitTransientCommandBuffer(whiteImgCmdBuff); // submit and wait
	vmaDestroyBuffer(m_vmaAllocator, whiteStagingBuffer.vkBuffer, whiteStagingBuffer.allocation);

	// fallback texture sampler
	VkSamplerCreateInfo samplerInfo
	{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.compareEnable = VK_FALSE
	};
	VkSampler sampler = nullptr;
	if (vkCreateSampler(m_device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
	{
		showError("Unable to create texture sampler");
		return false;
	}

	// store sampler and get ID
	m_samplers.push_back(sampler);
	uint32_t whiteSamplerId = m_samplers.size();

	m_textures.push_back(Texture{
		.imageId = m_whitePixelImageId,
		.samplerId = whiteSamplerId
		});

	//std::string gltfPath = "D:/glTF-Sample-Models/2.0/VC/glTF/VC.gltf";
	//gltfPath = "";
	//loadGltf("D:\\glTF-Sample-Models\\2.0\\DamagedHelmet\\glTF\\DamagedHelmet.gltf");
	//loadGltf("D:/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf");
	//loadGltf("D:/gltf Models/barn/scene.gltf");
	//loadGltf("S:/projects/boiler-3d/data/littlest_tokyo/glTF/littlest_tokyo.gltf");
	//loadGltf("D:/gltf Models/mario_kart_8_deluxe_-_los_angeles_laps_tour/scene.gltf");
	loadGltf("assets/modular-demo/modular-demo.gltf"); // Check your CWD

	// scale root node
	Node &root = m_nodeWorld.getNode(m_rootNodeId);
	//root.setScale(glm::vec3(0.01, 0.01, 0.01));
	//root.setTranslation(glm::vec3(0, 1, 0));

	// staging buffers for geo data
	GPUBuffer vertexBufferStage = createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vertexBufferBytes, true, VMA_MEMORY_USAGE_AUTO);
	if (!vertexBufferStage.vkBuffer)
	{
		showError("Error creating vertex staging buffer");
		return false;
	}
	GPUBuffer indexBufferStage = createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, indexBufferBytes, true, VMA_MEMORY_USAGE_AUTO);
	if (!indexBufferStage.vkBuffer)
	{
		showError("Error creating index staging buffer");
		return false;
	}
	// device-local buffers
	GPUBuffer vertexBuffer = createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, vertexBufferBytes, false, VMA_MEMORY_USAGE_AUTO);
	if (!vertexBuffer.vkBuffer)
	{
		showError("Error creating vertex buffer");
		return false;
	}
	m_vertexBufferId = addBuffer(vertexBuffer);
	mapCopyBufferData(vertexBufferStage, 0, m_vertices.data(), vertexBufferBytes);

	GPUBuffer indexBuffer = createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, indexBufferBytes, false, VMA_MEMORY_USAGE_AUTO);
	if (!indexBuffer.vkBuffer)
	{
		showError("Error creating index buffer");
		return false;
	}
	m_indexBufferId = addBuffer(indexBuffer);
	mapCopyBufferData(indexBufferStage, 0, m_indices.data(), indexBufferBytes);

	// copy staged geo data to VRAM
	VkCommandBuffer geoCmdBuffer = startTransientCommandBuffer();
	VkBufferCopy buffCopyVerts{ .srcOffset = 0, .dstOffset = 0, .size = vertexBufferBytes };
	vkCmdCopyBuffer(geoCmdBuffer, vertexBufferStage.vkBuffer, vertexBuffer.vkBuffer, 1, &buffCopyVerts);
	VkBufferCopy buffCopyIndices{ .srcOffset = 0, .dstOffset = 0, .size = indexBufferBytes };
	vkCmdCopyBuffer(geoCmdBuffer, indexBufferStage.vkBuffer, indexBuffer.vkBuffer, 1, &buffCopyIndices);
	submitTransientCommandBuffer(geoCmdBuffer); // submit and wait

	// delete staging geo buffers
	vmaDestroyBuffer(m_vmaAllocator, vertexBufferStage.vkBuffer, vertexBufferStage.allocation);
	vmaDestroyBuffer(m_vmaAllocator, indexBufferStage.vkBuffer, indexBufferStage.allocation);

	updateTextureDescriptors();

	// material buffer
	const size_t matDataBytes = m_materials.size() * sizeof(Material);
	GPUBuffer matBuffer = createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, matDataBytes, true, VMA_MEMORY_USAGE_AUTO);
	if (!matBuffer.vkBuffer)
	{
		showError("Error creating material buffer");
		return false;
	}
	m_matBufferId = addBuffer(matBuffer);
	mapCopyBufferData(matBuffer, 0, m_materials.data(), matDataBytes);

	return true;
}

std::vector<Image> Application::loadImages(const tg3_model &model, const std::filesystem::path &imageDir)
{
	std::vector<Image> images(model.images_count);
	for (int i = 0; i < model.images_count; ++i)
	{
		Image &img = images[i];
		std::filesystem::path imagePath = imageDir / model.images[i].uri.data;
		std::print("Loading image {}/{}: {}\n", i + 1, model.images_count, model.images[i].uri.data);
		img.data = stbi_load(imagePath.string().c_str(), &img.width, &img.height, &img.channels, 4);
		if (!img.data)
		{
			showError("Failed to load image: " + imagePath.string());
		}
	}
	return images;
}

std::vector<uint32_t> Application::loadSamplers(const tg3_model &model)
{
	std::vector<uint32_t> samplerIds(model.samplers_count);
	for (int i = 0; i < model.samplers_count; ++i)
	{
		const tg3_sampler &tg3Sampler = model.samplers[i];
		static const std::unordered_map<int32_t, std::tuple<VkFilter, VkSamplerMipmapMode, float>> filterMap
		{
			{ TG3_TEXTURE_FILTER_NEAREST, { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.25f } },
			{ TG3_TEXTURE_FILTER_LINEAR, { VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.25f } },
			{ TG3_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR, { VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_LOD_CLAMP_NONE } },
			{ TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST, { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_LOD_CLAMP_NONE } },
			{ TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR, { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_LOD_CLAMP_NONE } },
			{ TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST, { VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_LOD_CLAMP_NONE } }
		};
		static const std::unordered_map<int32_t, VkSamplerAddressMode> wrapMap
		{
			{ TG3_TEXTURE_WRAP_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT },
			{ TG3_TEXTURE_WRAP_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE },
			{ TG3_TEXTURE_WRAP_MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT }
		};

		VkSamplerCreateInfo samplerInfo
		{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = (tg3Sampler.mag_filter == -1) ? VK_FILTER_LINEAR : std::get<0>(filterMap.at(tg3Sampler.mag_filter)),
			.minFilter = (tg3Sampler.min_filter == -1) ? VK_FILTER_LINEAR : std::get<0>(filterMap.at(tg3Sampler.min_filter)),
			.mipmapMode = (tg3Sampler.min_filter == -1) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : std::get<1>(filterMap.at(tg3Sampler.min_filter)),
			.addressModeU = (tg3Sampler.wrap_s == -1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : wrapMap.at(tg3Sampler.wrap_s),
			.addressModeV = (tg3Sampler.wrap_t == -1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : wrapMap.at(tg3Sampler.wrap_t),
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.compareEnable = VK_FALSE,
			.minLod = 0.0f,
			.maxLod = (tg3Sampler.min_filter == -1) ? VK_LOD_CLAMP_NONE : std::get<2>(filterMap.at(tg3Sampler.min_filter))
		};

		VkSampler sampler = nullptr;
		if (vkCreateSampler(m_device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
		{
			showError("Unable to create texture sampler");
			samplerIds[i] = m_textures[0].samplerId; // fallback texture's sampler
		}
		else
		{
			m_samplers.push_back(sampler);
			samplerIds[i] = m_samplers.size();
		}
	}
	return samplerIds;
}

std::vector<uint32_t> Application::loadTextures(const tg3_model &model, const std::vector<uint32_t> &imageIds, const std::vector<uint32_t> &samplerIds)
{
	assert(m_textures.size() + model.textures_count <= MaxTextures && "Exceeding max texture count");
	std::vector<uint32_t> textureIds(model.textures_count);
	for (int i = 0; i < model.textures_count; ++i)
	{
		const tg3_texture &tex = model.textures[i];
		m_textures.push_back(
			Texture
			{
				.imageId = imageIds[tex.source],
				.samplerId = samplerIds[tex.sampler]
			});
		textureIds[i] = m_textures.size();
	}
	return textureIds;
}

std::vector<uint32_t> Application::uploadImages(const std::vector<Image> &images)
{
	VkCommandBuffer commandBuffer = startTransientCommandBuffer();
	std::vector<GPUBuffer> stagingBuffers;
	stagingBuffers.reserve(images.size());

	// upload images to GPU textures
	std::vector<uint32_t> imageIds(images.size());
	for (int i = 0; i < images.size(); ++i)
	{
		const Image &image = images[i];
		if (image.data)
		{
			auto [imageId, stagingTexBuffer] = createImage(commandBuffer, image.data, image.width, image.height, 4);
			imageIds[i] = imageId;
			stagingBuffers.push_back(stagingTexBuffer);
		}
		else
		{
			imageIds[i] = m_whitePixelImageId; // fallback to white pixel texture
		}
	}

	submitTransientCommandBuffer(commandBuffer); // submit and wait

	// clean up the staging buffers
	for (GPUBuffer &stageBuff : stagingBuffers)
	{
		vmaDestroyBuffer(m_vmaAllocator, stageBuff.vkBuffer, stageBuff.allocation);
	}
	return imageIds;
}

VkCommandBuffer Application::startTransientCommandBuffer()
{
	// allocate the transient command buffer
	VkCommandBufferAllocateInfo cmdAllocInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = m_commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	VkCommandBuffer commandBuffer = nullptr;
	if (vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &commandBuffer) != VK_SUCCESS)
	{
		showError("Unable to allocate command buffer");
		return nullptr;
	}

	// begin the command buffer
	VkCommandBufferBeginInfo beginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
	{
		showError("Unable to begin command buffer");
		vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
		return nullptr;
	}

	return commandBuffer;
}

void Application::submitTransientCommandBuffer(VkCommandBuffer commandBuffer)
{
	vkEndCommandBuffer(commandBuffer);

	// TODO: Submit on a transfer queue
	VkSubmitInfo submitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &commandBuffer
	};

	vkQueueSubmit(m_gfxQueue, 1, &submitInfo, nullptr);
	vkQueueWaitIdle(m_gfxQueue);
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void Application::loadGltf(const std::string &filepath)
{
	if (!std::filesystem::exists(filepath))
	{
		std::print("File doesn't exist: {}\n", filepath);
		return;
	}

	std::print(" ** Loading GLTF: {}\n", filepath);
	// load and parse GLTF
	tg3_model model;
	tg3_parse_options opts;
	tg3_error_stack errors;

	tg3_parse_options_init(&opts);
	tg3_error_stack_init(&errors);
	tg3_error_code parseResult = tg3_parse_file(&model, &errors, filepath.c_str(), filepath.size(), &opts);
	if (parseResult != TG3_OK)
	{
		std::print("Error parsing glTF file, errors found:\n");
		for (int i = 0; i < errors.count; ++i)
		{
			std::print("{}\n", errors.entries[i].message);
		}
		tg3_error_stack_free(&errors);
		return;
	}
	tg3_error_stack_free(&errors);

	std::filesystem::path imageDir = std::filesystem::path(filepath).parent_path();
	std::vector<Image> images = loadImages(model, imageDir); // load images into RAM
	std::vector<uint32_t> imageIds = uploadImages(images); // upload images to VRAM
	// free image memory after uploading to VRAM
	for (const Image &image : images)
	{
		stbi_image_free(image.data);
	}

	std::vector<uint32_t> samplerIds = loadSamplers(model); // samplers required for shaders
	std::vector<uint32_t> textureIds = loadTextures(model, imageIds, samplerIds); // image/sampler combinations
	std::vector<uint32_t> materialIds = loadMaterials(model, textureIds); // materials reference textureIds
	std::vector<uint32_t> meshIds = loadMeshes(model, materialIds); // meshes/submeshes reference materials

	// import scene nodes
	const tg3_scene *scene = &model.scenes[model.default_scene != -1
		? model.default_scene : 0];

	// iterate over the scene's roots nodes, attach to existing root node (if present)
	for (int i = 0; i < scene->nodes_count; ++i)
	{
		uint32_t nodeId = importNode(m_nodeWorld, model, scene->nodes[i], 0, m_lastRootNodeId, meshIds);
		if (!m_rootNodeId) // first root node
		{
			m_rootNodeId = nodeId;
			m_lastRootNodeId = nodeId;
		}
		else // subsequent root nodes, link to previous root node
		{
			m_lastRootNodeId = nodeId;
		}
	}
	tg3_model_free(&model);
	std::print("GLTF Loading Successful\n\n");
}

uint32_t Application::importNode(NodeWorld &nodeWorld, const tg3_model &model, int32_t nodeIndex, uint32_t parentId, uint32_t prevSiblingId, std::vector<uint32_t> &meshIds)
{
	const tg3_node &tg3Node = model.nodes[nodeIndex];

	auto [node, nodeId] = nodeWorld.createNode();
	node.parentId = parentId;

	if (tg3Node.has_matrix)
	{
		glm::mat4 transform(1);
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
	else if (tg3Node.camera != -1)
	{
		// TODO: Support multiple camera objects
		const tg3_camera &tg3Camera = model.cameras[tg3Node.camera];
		m_camera.fovY = tg3Camera.perspective.yfov;
		m_camera.nearPlane = tg3Camera.perspective.znear;
		m_camera.farPlane = tg3Camera.perspective.zfar;
		m_cameraNodeId = nodeId;

		m_camera.forward = node.getRotation() * glm::vec3(0, 0, -1);
		m_camera.right = node.getRotation() * glm::vec3(1, 0, 0);
		m_camera.up = glm::cross(m_camera.right, m_camera.forward);
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

void Application::shutdown()
{
	// wait in case resources are in use
	vkDeviceWaitIdle(m_device);

	// clean up descriptor layouts and pool
	vkDestroyDescriptorSetLayout(m_device, m_globalDSLayout, nullptr);
	vkDestroyDescriptorPool(m_device, m_descPool, nullptr);

	// delete textures and samplers
	for (auto &img : m_images)
	{
		vkDestroyImageView(m_device, img.imageView, nullptr);
		vkDestroyImage(m_device, img.image, nullptr);
		vmaFreeMemory(m_vmaAllocator, img.allocation);
	}
	for (VkSampler sampler : m_samplers)
	{
		vkDestroySampler(m_device, sampler, nullptr);
	}

	// delete buffers
	for (auto &buff : m_buffers)
	{
		vkDestroyBuffer(m_device, buff.vkBuffer, nullptr);
		vmaFreeMemory(m_vmaAllocator, buff.allocation);
	}

	// frame / sync object cleanup
	vkDestroySemaphore(m_device, m_timelineSemaphore, nullptr);
	for (auto &res : m_frameResources)
	{
		vkDestroySemaphore(m_device, res.imageAcquiredSemaphore, nullptr);
		vkDestroyCommandPool(m_device, res.commandPool, nullptr); // destroys buffers implicitly

		// indirect draw
		vmaUnmapMemory(m_vmaAllocator, res.indirectDrawBuffer.allocation);
		vkDestroyBuffer(m_device, res.indirectDrawBuffer.vkBuffer, nullptr);
		vmaFreeMemory(m_vmaAllocator, res.indirectDrawBuffer.allocation);

		// render items
		vmaUnmapMemory(m_vmaAllocator, res.renderItemBuffer.allocation);
		vkDestroyBuffer(m_device, res.renderItemBuffer.vkBuffer, nullptr);
		vmaFreeMemory(m_vmaAllocator, res.renderItemBuffer.allocation);
	}
	// one-time use command buffer pool
	vkDestroyCommandPool(m_device, m_commandPool, nullptr);

	// pipeline cleanup
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
	if (m_pipeline)
	{
		vkDestroyPipeline(m_device, m_pipeline, nullptr);
	}

	// cleanup shaders
	vkDestroyShaderModule(m_device, m_vertShader, nullptr);
	vkDestroyShaderModule(m_device, m_fragShader, nullptr);

	// cleanup swapchain
	destroySwapchain();

	// VMA
	vmaDestroyAllocator(m_vmaAllocator);

	// cleanup Vulkan
	vkDestroySurfaceKHR(m_vulkanInstance, m_surface, nullptr);
	vkDestroyDevice(m_device, nullptr);
	vkDestroyInstance(m_vulkanInstance, nullptr);
	volkFinalize();

	// cleanup SDL
	if (m_window)
	{
		SDL_DestroyWindow(m_window);
	}
	SDL_Quit();
}

void Application::run()
{
	assert(m_cameraNodeId && "Camera node must be initialized");
	updateProjectionMatrix();

	m_running = true;
	const bool *keys = SDL_GetKeyboardState(nullptr);

	uint64_t prevTime = SDL_GetTicks();
	while (m_running)
	{
		uint64_t nowTime = SDL_GetTicks();
		const float deltaTime = (nowTime - prevTime) / 1000.0f;
		prevTime = nowTime;

		SDL_Event event{ 0 };
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT)
			{
				m_running = false;
				break;
			}
			else if (event.type == SDL_EVENT_WINDOW_RESIZED)
			{
				m_width = event.window.data1;
				m_height = event.window.data2;
				updateProjectionMatrix();
				break;
			}
			else if (event.type == SDL_EVENT_MOUSE_MOTION)
			{
				m_mouseXRel += static_cast<float>(event.motion.xrel);
				m_mouseYRel += static_cast<float>(event.motion.yrel);
			}
			else if (event.type == SDL_EVENT_KEY_UP)
			{
				if (event.key.scancode == SDL_SCANCODE_GRAVE)
				{
					if (m_camera.type == CameraType::firstPerson)
					{
						m_camera.type = CameraType::orbit;
						SDL_SetWindowRelativeMouseMode(m_window, false);
					}
					else
					{
						m_camera.type = CameraType::firstPerson;
						SDL_SetWindowRelativeMouseMode(m_window, true);
					}
				}
			}
		}

		// handle basic cam movement
		constexpr float speed = 4.0f;
		constexpr float epsilon = 0.01f;
		constexpr float pitchLimit = glm::half_pi<float>() - epsilon;

		if (m_camera.type == CameraType::orbit)
		{
			if (keys[SDL_SCANCODE_W])
			{
				m_camera.distance -= speed * deltaTime;
				m_camera.distance = std::max(m_camera.distance, epsilon);
			}
			if (keys[SDL_SCANCODE_S])
			{
				m_camera.distance += speed * deltaTime;
			}
			if (keys[SDL_SCANCODE_UP])
			{
				m_camera.pitch += speed * deltaTime;
				m_camera.pitch = std::clamp(m_camera.pitch, -pitchLimit, pitchLimit);
			}
			if (keys[SDL_SCANCODE_DOWN])
			{
				m_camera.pitch -= speed * deltaTime;
				m_camera.pitch = std::clamp(m_camera.pitch, -pitchLimit, pitchLimit);
			}
		}
		else if (m_camera.type == CameraType::firstPerson)
		{
			Node &camNode = m_nodeWorld.getNode(m_cameraNodeId);
			glm::vec3 translation = camNode.getTranslation();
			if (keys[SDL_SCANCODE_A])
			{
				translation -= m_camera.right * speed * deltaTime;
			}
			if (keys[SDL_SCANCODE_D])
			{
				translation += m_camera.right * speed * deltaTime;
			}
			if (keys[SDL_SCANCODE_W])
			{
				translation += m_moveDirection * speed * deltaTime;
			}
			if (keys[SDL_SCANCODE_S])
			{
				translation -= m_moveDirection * speed * deltaTime;
			}
			camNode.setTranslation(translation);
		}

		updateViewMatrix();
		render();
	}
}

bool Application::initializeVulkan()
{
	if (!createVulkanInstance())
	{
		showError("Couldn't create a vulkan instance");
		return false;
	}

	if (!createSurface())
	{
		showError("Couldn't create window surface");
		return false;
	}

	if (m_physicalDevice = findPhysicalDevice(); !m_physicalDevice)
	{
		showError("Unable to find an appropriate physical device");
		return false;
	}

	if (!findGraphicsQueue())
	{
		showError("Unable to find a compatible graphics queue");
		return false;
	}

	if (!createDevice(m_physicalDevice))
	{
		showError("Couldn't create the logical GPU device");
		return false;
	}

	if (!initializeVMA())
	{
		showError("Unable to create Vulkan Memory Allocator");
		return false;
	}

	if (!createSwapchain(m_width, m_height))
	{
		showError("Unable to create swapchain");
		return false;
	}

	if (!createShaders())
	{
		showError("Error creating shader modules");
		return false;
	}

	if (!createDescriptorSets())
	{
		showError("Error creating descriptor sets");
		return false;
	}

	if (m_pipeline = createGraphicsPipeline(); !m_pipeline)
	{
		showError("Unable to initialize the graphics pipeline");
		return false;
	}

	if (!createSyncResources())
	{
		showError("Couldn't create the sync related resources");
		return false;
	}

	if (!createCommandBuffers())
	{
		showError("Couldn't create command buffer objects");
		return false;
	}

	if (!createIndirectDrawBuffers())
	{
		showError("Couldn't create buffers for indirect-drawing");
		return false;
	}

	return true;
}

bool Application::createVulkanInstance()
{
	// Initialize Volk and load Vk function pointers
	if (volkInitialize() != VK_SUCCESS)
	{
		showError("Error initializing Volk");
		return false;
	}

	// Create the vulkan application instance
	VkApplicationInfo appInfo
	{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "My First Triangle",
		.apiVersion = VulkanVersion,
	};

	uint32_t instExtCount = 0;
	const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&instExtCount);

	std::vector<const char *> requestedLayers
	{
		"VK_LAYER_KHRONOS_validation"
	};

	VkInstanceCreateInfo instCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = static_cast<uint32_t>(requestedLayers.size()),
		.ppEnabledLayerNames = requestedLayers.data(),
		.enabledExtensionCount = instExtCount,
		.ppEnabledExtensionNames = extensions
	};

	if (vkCreateInstance(&instCreateInfo, nullptr, &m_vulkanInstance) != VK_SUCCESS)
	{
		return false;
	}

	volkLoadInstance(m_vulkanInstance);
	return true;
}

bool Application::createSurface()
{
	if (!SDL_Vulkan_CreateSurface(m_window, m_vulkanInstance, nullptr, &m_surface))
	{
		return false;
	}
	return true;
}

VkPhysicalDevice Application::findPhysicalDevice()
{
	// enumerate all physical devices
	uint32_t physDeviceCount = 0;
	vkEnumeratePhysicalDevices(m_vulkanInstance, &physDeviceCount, nullptr);
	std::vector<VkPhysicalDevice> physicalDevices(physDeviceCount);
	vkEnumeratePhysicalDevices(m_vulkanInstance, &physDeviceCount, physicalDevices.data());

	VkPhysicalDevice physicalDevice = nullptr;
	if (physDeviceCount)
	{
		// if you have issues, you can always just hardcode a GPU index while learning
		physicalDevice = physicalDevices[0]; // default to first GPU
		// look through list and see if a dGPU exists
		for (auto &pDev : physicalDevices)
		{
			VkPhysicalDeviceProperties props{};
			vkGetPhysicalDeviceProperties(pDev, &props);
			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				physicalDevice = pDev;
				break;
			}
		}
	}

	// ensure the desired swapchain format is supported
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, surfaceFormats.data());

	bool formatSupported = false;
	for (const VkSurfaceFormatKHR &surfFormat : surfaceFormats)
	{
		if (surfFormat.format == SwapchainFormat)
		{
			formatSupported = true;
			break;
		}
	}
	if (!formatSupported)
	{
		showError("Requested swapchain format is not supported by the surface");
		return nullptr;
	}

	return physicalDevice;
}

bool Application::findGraphicsQueue()
{
	// eventually we'll have more complex queue lookup for presentation, etc
	// grab all of the queue families
	uint32_t queueFamCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties2(m_physicalDevice, &queueFamCount, nullptr);
	std::vector<VkQueueFamilyProperties2> queueFamProps(queueFamCount, { VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
	vkGetPhysicalDeviceQueueFamilyProperties2(m_physicalDevice, &queueFamCount, queueFamProps.data());

	for (int currentFamIdx = 0; currentFamIdx < queueFamProps.size(); currentFamIdx++)
	{
		// ensure it has presentation support
		VkBool32 hasPresentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, currentFamIdx, m_surface, &hasPresentSupport);

		const auto &props = queueFamProps[currentFamIdx];
		// ensure this is a GRAPHICS queue with presentation support
		if (props.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT && hasPresentSupport)
		{
			m_gfxQueueFamIdx = currentFamIdx;
			return true;
		}
	}
	return false;
}


bool Application::createDevice(VkPhysicalDevice physicalDevice)
{
	float queuePriority = 1.0f;
	std::vector<uint32_t> queueFamiles{ m_gfxQueueFamIdx };

	VkDeviceQueueCreateInfo gfxQueueInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = m_gfxQueueFamIdx,
		.queueCount = 1,
		.pQueuePriorities = &queuePriority
	};

	// query suppoted features
	/*VkPhysicalDeviceVulkan14Features supportedFeatures14{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext = nullptr};
	VkPhysicalDeviceVulkan13Features supportedFeatures13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &supportedFeatures14 };
	VkPhysicalDeviceVulkan12Features supportedFeatures12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supportedFeatures13 };
	VkPhysicalDeviceFeatures2 supportedFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supportedFeatures12 };
	vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);

	// check if what we need is supported
	if (!supportedFeatures14.hostImageCopy ||
		!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2 ||
		!supportedFeatures12.timelineSemaphore || !supportedFeatures12.bufferDeviceAddress ||
		!supportedFeatures12.scalarBlockLayout || !supportedFeatures12.descriptorIndexing ||
		!supportedFeatures12.descriptorBindingSampledImageUpdateAfterBind ||
		!supportedFeatures12.descriptorBindingPartiallyBound ||
		!supportedFeatures12.runtimeDescriptorArray ||
		!supportedFeatures.features.shaderInt64)
	{
		showError("Physical device doesn't meet the feature requirements");
		return false;
	}*/
	VkPhysicalDeviceVulkan14Features supportedFeatures14{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext = nullptr };
	VkPhysicalDeviceVulkan13Features supportedFeatures13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &supportedFeatures14 };
	VkPhysicalDeviceVulkan12Features supportedFeatures12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supportedFeatures13 };
	VkPhysicalDeviceFeatures2 supportedFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supportedFeatures12 };
	vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);

	// check if what we need is supported
	if (!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2 ||
		!supportedFeatures12.timelineSemaphore || !supportedFeatures12.bufferDeviceAddress ||
		!supportedFeatures12.scalarBlockLayout || !supportedFeatures12.descriptorIndexing ||
		!supportedFeatures12.descriptorBindingSampledImageUpdateAfterBind ||
		!supportedFeatures12.descriptorBindingPartiallyBound ||
		!supportedFeatures12.runtimeDescriptorArray ||
		!supportedFeatures.features.shaderInt64 ||
		!supportedFeatures.features.multiDrawIndirect)
	{
		showError("Physical device doesn't meet the feature requirements");
		return false;
	}

	// produce a separate features struct chain for device creation
	VkPhysicalDeviceVulkan14Features features14
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = nullptr,
		.hostImageCopy = VK_FALSE, // enable if you have support
	};
	VkPhysicalDeviceVulkan13Features features13
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &features14,
		.synchronization2 = VK_TRUE,
		.dynamicRendering = VK_TRUE,
	};
	VkPhysicalDeviceVulkan12Features features12
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &features13,
		.descriptorIndexing = VK_TRUE,
		.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
		.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
		.descriptorBindingPartiallyBound = VK_TRUE,
		.runtimeDescriptorArray = VK_TRUE,
		.scalarBlockLayout = VK_TRUE,
		.timelineSemaphore = VK_TRUE,
		.bufferDeviceAddress = VK_TRUE,
	};
	VkPhysicalDeviceFeatures2 features
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features12,
		.features
		{
			.multiDrawIndirect = VK_TRUE,
			.shaderInt64 = VK_TRUE
		}
	};

	const std::vector<const char *> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo devCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &gfxQueueInfo,
		.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
		.ppEnabledExtensionNames = deviceExtensions.data(),
		.pEnabledFeatures = nullptr // features struct chain is set in pNext
	};

	if (vkCreateDevice(physicalDevice, &devCreateInfo, nullptr, &m_device) != VK_SUCCESS)
	{
		return false;
	}

	// grab the VkQueue object finally
	vkGetDeviceQueue(m_device, m_gfxQueueFamIdx, 0, &m_gfxQueue);
	if (!m_gfxQueue)
	{
		showError("Couldn't get the graphics queue");
		return false;
	}
	return true;
}

bool Application::initializeVMA()
{
	VmaVulkanFunctions vmaFuncInfo{};
	VmaAllocatorCreateInfo vmaAllocInfo
	{
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = m_physicalDevice,
		.device = m_device,
		.pVulkanFunctions = &vmaFuncInfo,
		.instance = m_vulkanInstance,
		.vulkanApiVersion = VulkanVersion
	};

	// vma can import directly from volk
	vmaImportVulkanFunctionsFromVolk(&vmaAllocInfo, &vmaFuncInfo);

	if (vmaCreateAllocator(&vmaAllocInfo, &m_vmaAllocator) != VK_SUCCESS)
	{
		return false;
	}
	return true;
}

bool Application::createSwapchain(uint32_t width, uint32_t height)
{
	m_swapchainWidth = width;
	m_swapchainHeight = height;

	VkSurfaceCapabilitiesKHR surfaceCaps{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCaps) != VK_SUCCESS)
	{
		showError("Couldn't get the surface capabilities");
		return false;
	}

	VkSwapchainCreateInfoKHR swapchainCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = m_surface,
		.minImageCount = surfaceCaps.minImageCount,
		.imageFormat = SwapchainFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent{.width = m_swapchainWidth, .height = m_swapchainHeight },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR
	};

	if (vkCreateSwapchainKHR(m_device, &swapchainCreateInfo, nullptr, &m_swapchain) != VK_SUCCESS)
	{
		showError("Error creating swapchain");
		return false;
	}

	// grab the swapchain images
	uint32_t imageCount = 0;
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
	m_swapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());
	m_swapchainImageViews.resize(imageCount);

	// create the swapchain image views
	for (size_t i = 0; i < m_swapchainImages.size(); ++i)
	{
		VkImageViewCreateInfo imgViewInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = m_swapchainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = SwapchainFormat,
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.levelCount = 1,
				.layerCount = 1
			}
		};

		if (vkCreateImageView(m_device, &imgViewInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS)
		{
			showError("Error creating swapchain image view");
			return false;
		}
	}

	// semaphores used to signal render completion
	m_renderCompleteSemaphores.resize(m_swapchainImages.size());
	for (VkSemaphore &semaphore : m_renderCompleteSemaphores)
	{
		VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
		{
			showError("Error creating the render-complete semaphore");
			return false;
		}
	}

	// create depth image
	VkImageCreateInfo depthCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = DepthFormat,
		.extent{.width = m_swapchainWidth, .height = m_swapchainHeight, .depth = 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VmaAllocationCreateInfo allocInfo
	{
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO
	};
	if (vmaCreateImage(m_vmaAllocator, &depthCreateInfo, &allocInfo, &m_depthImage, &m_depthImageAllocation, nullptr) != VK_SUCCESS)
	{
		showError("Error allocating depth image");
		return false;
	}

	VkImageViewCreateInfo depthImgViewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = m_depthImage,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = DepthFormat,
		.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1}
	};
	if (vkCreateImageView(m_device, &depthImgViewInfo, nullptr, &m_depthImageView) != VK_SUCCESS)
	{
		showError("Error creating depth image view");
		return false;
	}

	return true;
}

void Application::destroySwapchain()
{
	for (VkImageView swapchainImgView : m_swapchainImageViews)
	{
		vkDestroyImageView(m_device, swapchainImgView, nullptr);
	}
	m_swapchainImageViews.clear();

	// destroy render-complete ssemaphores
	for (VkSemaphore &semaphore : m_renderCompleteSemaphores)
	{
		vkDestroySemaphore(m_device, semaphore, nullptr);
	}
	m_renderCompleteSemaphores.clear();

	if (m_swapchain)
	{
		vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
		m_swapchain = nullptr;
	}

	// destroy the depth buffer along with the swapchain
	if (m_depthImageView)
	{
		vkDestroyImageView(m_device, m_depthImageView, nullptr);
		vmaDestroyImage(m_vmaAllocator, m_depthImage, m_depthImageAllocation);
		m_depthImageView = nullptr;
	}
}

VkShaderModule Application::createShaderModule(const std::string &fileName, shaderc_shader_kind kind) const
{
	// read shader file from disk
	const std::string shaderPath = "src/shaders/" + fileName;
	const std::string src = readTextFile(shaderPath);
	if (src.empty())
	{
		showError("Specified shader file doesn't exist: " + shaderPath);
		return nullptr;
	}

	// compile the shader to SPIR-V
	std::cout << "Compiling shader: " << shaderPath << std::endl;
	shaderc::Compiler compiler;
	shaderc::CompileOptions opts;
	opts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
	opts.SetTargetSpirv(shaderc_spirv_version_1_6);
	opts.SetOptimizationLevel(shaderc_optimization_level_performance);
	shaderc::CompilationResult result = compiler.CompileGlslToSpv(src, kind, fileName.c_str(), opts);

	if (result.GetCompilationStatus() != shaderc_compilation_status_success)
	{
		std::cerr << "Shader Compilation Error: " << result.GetErrorMessage() << std::endl;
		return nullptr;
	}
	std::vector<uint32_t> spv = { result.cbegin(), result.cend() };

	// pass spir-v to vulkan and create shader-module
	VkShaderModuleCreateInfo moduleCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = spv.size() * sizeof(uint32_t),
		.pCode = spv.data()
	};
	VkShaderModule shaderModule = nullptr;
	if (vkCreateShaderModule(m_device, &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		showError("Error creating shader module");
		return nullptr;
	}
	return shaderModule;
}

bool Application::createShaders()
{
	// create the shader modules that we'll need for the graphics pipeline
	if (m_vertShader = createShaderModule("shader.vert", shaderc_vertex_shader); !m_vertShader)
	{
		return false;
	}
	if (m_fragShader = createShaderModule("shader.frag", shaderc_fragment_shader); !m_fragShader)
	{
		return false;
	}
	return true;
}

VkPipeline Application::createGraphicsPipeline()
{
	// need to define a pipeline layout
	VkPushConstantRange pushConstRange{ .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
									   .offset = 0,
									   .size = sizeof(FrameConstants) };

	std::array<VkDescriptorSetLayout, 1> dsLayouts{ m_globalDSLayout };

	VkPipelineLayoutCreateInfo pipelineLayoutInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = dsLayouts.size(),
		.pSetLayouts = dsLayouts.data(),
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstRange
	};

	if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
	{
		showError("Unable to create the pipeline layout");
		return nullptr;
	}

	// configure the shader stages struct
	const char *entryPoint = "main";
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = m_vertShader,
			.pName = entryPoint
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = m_fragShader,
			.pName = entryPoint
		}
	};

	// vertex pulling, don't define vertex input details
	VkPipelineVertexInputStateCreateInfo vertInputInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
	};

	// input assembly, we'll be drawing triangle lists
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};

	// depth/stencil configuration
	VkPipelineDepthStencilStateCreateInfo depthStencilInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.stencilTestEnable = VK_FALSE
	};

	// dynamic rendering allows to set this up...dynamically
	// we still need this struct though
	VkPipelineViewportStateCreateInfo viewportInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = nullptr,
		.scissorCount = 1,
		.pScissors = nullptr
	};

	// rasterizer settings
	VkPipelineRasterizationStateCreateInfo rasterInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	// No multisampling
	VkPipelineMultisampleStateCreateInfo multiSampleInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};

	// Alpha-blending (disabled for now), still need
	// attachment info and write mask
	VkPipelineColorBlendAttachmentState attachState
	{
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};
	VkPipelineColorBlendStateCreateInfo blendInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachState
	};

	// enable dynamic state
	std::vector<VkDynamicState> dynamicState
	{
		VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicStateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = static_cast<uint32_t>(dynamicState.size()),
		.pDynamicStates = dynamicState.data()
	};

	// structure required for dynamic rendering
	VkPipelineRenderingCreateInfo renderInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &SwapchainFormat,
		.depthAttachmentFormat = DepthFormat
	};

	// Create the graphics pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo
	{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderInfo,
		.stageCount = static_cast<uint32_t>(shaderStages.size()),
		.pStages = shaderStages.data(),
		.pVertexInputState = &vertInputInfo,
		.pInputAssemblyState = &inputAssemblyInfo,
		.pViewportState = &viewportInfo,
		.pRasterizationState = &rasterInfo,
		.pMultisampleState = &multiSampleInfo,
		.pDepthStencilState = &depthStencilInfo,
		.pColorBlendState = &blendInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = m_pipelineLayout,
		.renderPass = VK_NULL_HANDLE,
	};
	if (vkCreateGraphicsPipelines(m_device, nullptr, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
	{
		showError("Error creating the pipeline");
		return nullptr;
	}
	return m_pipeline;
}

bool Application::createSyncResources()
{
	VkSemaphoreTypeCreateInfo semaphoreTypeInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = MaxFramesInFlight
	};
	VkSemaphoreCreateInfo semaphoreInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &semaphoreTypeInfo
	};
	if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_timelineSemaphore) != VK_SUCCESS)
	{
		showError("Unable to create the timeline semaphore");
		return false;
	}

	// per-frame image-acquire semaphores
	for (FrameResources &res : m_frameResources)
	{
		// create the binary semaphores
		VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &res.imageAcquiredSemaphore) != VK_SUCCESS)
		{
			showError("Error creating the per-frame image-acquire semaphore");
			return false;
		}
	}

	return true;
}

bool Application::createCommandBuffers()
{
	// create a command pool for single use command buffers
	VkCommandPoolCreateInfo poolInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = m_gfxQueueFamIdx
	};
	if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
	{
		showError("Unable to create command buffer pool");
		return false;
	}

	for (FrameResources &res : m_frameResources)
	{
		// we'll give each frame its own pool, faster cmd buffer resets this way
		VkCommandPoolCreateInfo poolInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = m_gfxQueueFamIdx
		};
		if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &res.commandPool) != VK_SUCCESS)
		{
			showError("Unable to create command buffer pool");
			return false;
		}

		// create the command buffer for this frame
		VkCommandBufferAllocateInfo cmdAllocInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = res.commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};

		if (vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &res.commandBuffer) != VK_SUCCESS)
		{
			showError("Unable to allocate command buffer");
			return false;
		}
	}
	return true;
}

void Application::render()
{
	// first check if our swapchain is still valid
	if (m_requireSwapchainRecreate)
	{
		vkDeviceWaitIdle(m_device);
		destroySwapchain();
		createSwapchain(m_width, m_height);
		m_requireSwapchainRecreate = false;
	}

	const uint32_t frameResIndex = m_frameIndex++ % MaxFramesInFlight;
	const uint64_t signalValue = m_nextSignalValue++;
	const uint64_t waitValue = signalValue - MaxFramesInFlight;

	VkSemaphoreWaitInfo waitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &m_timelineSemaphore,
		.pValues = &waitValue
	};
	vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX);

	// now its safe to start recording commands
	FrameResources &res = m_frameResources[frameResIndex];
	vkResetCommandPool(m_device, res.commandPool, 0);

	// get the resources for this frame
	VkSemaphore imageAcquireSemaphore = m_frameResources[frameResIndex].imageAcquiredSemaphore;

	uint32_t imageIndex = 0;
	VkResult acquireResult = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, imageAcquireSemaphore, VK_NULL_HANDLE, &imageIndex);

	// handle resize and out-of-date images, may need swapchain recreate
	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_requireSwapchainRecreate = true;
		return;
	}
	else if (acquireResult == VK_SUBOPTIMAL_KHR)
	{
		// can render this frame, recreate next time around
		m_requireSwapchainRecreate = true;
	}

	// traverse entire scene and record MDI draw commands
	// push root nodes to render-stack
	m_nodeRenderStack.clear();
	uint32_t nodeId = m_rootNodeId;
	while (nodeId)
	{
		Node &node = m_nodeWorld.getNode(nodeId);
		m_nodeRenderStack.push_back({ &node, glm::mat4(1.0f) });
		nodeId = node.nextSiblingId;
	};

	uint32_t drawIndex = 0;
	while (!m_nodeRenderStack.empty())
	{
		auto [node, parentTransform] = m_nodeRenderStack.back();
		m_nodeRenderStack.pop_back();
		glm::mat4 matWorld = parentTransform * node->getTransform();

		// draw the associated mesh
		if (node->meshId)
		{
			Mesh &mesh = m_meshes[node->meshId - 1];
			for (SubMesh &subMesh : mesh.subMeshes)
			{
				// indirect draw command
				res.indirectDrawPtr[drawIndex] = VkDrawIndexedIndirectCommand
				{
					.indexCount = static_cast<uint32_t>(subMesh.indexCount),
					.instanceCount = 1,
					.firstIndex = static_cast<uint32_t>(subMesh.indexStart),
					.vertexOffset = static_cast<int32_t>(subMesh.vertexStart),
					.firstInstance = drawIndex
				};
				// per render-item data
				res.renderItemPtr[drawIndex] = RenderItem
				{
					.wvp = m_viewProjMatrix * matWorld,
					.worldMatrix = matWorld,
					.materialIndex = subMesh.materialId - 1
				};
				drawIndex++;
			}
		}

		// child nodes for processing
		uint32_t childNodeId = node->firstChildId;
		while (childNodeId)
		{
			Node &child = m_nodeWorld.getNode(childNodeId);
			m_nodeRenderStack.push_back({ &child, matWorld });
			childNodeId = child.nextSiblingId;
		}
	}

	// begin recording commands
	VkCommandBufferBeginInfo cmdBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	vkBeginCommandBuffer(res.commandBuffer, &cmdBeginInfo);

	// transition the color and depth images
	std::array<VkImageMemoryBarrier2, 2> layoutBarriers
	{
		VkImageMemoryBarrier2
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.image = m_swapchainImages[imageIndex],
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		},
		VkImageMemoryBarrier2
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, // both specified to control memory access at both stages (write)
			.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.image = m_depthImage,
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		}
	};
	VkDependencyInfo depInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = static_cast<uint32_t>(layoutBarriers.size()),
		.pImageMemoryBarriers = layoutBarriers.data()
	};
	vkCmdPipelineBarrier2(res.commandBuffer, &depInfo);

	// setup the attachments (color and depth) and begin rendering (dynamic)
	VkRenderingAttachmentInfo colorAttachInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = m_swapchainImageViews[imageIndex],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, // clear the image
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE, // keep data for presentation
		.clearValue{.color{0.01f, 0.01f, 0.01f, 1}}

	};
	VkRenderingAttachmentInfo depthAttachInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = m_depthImageView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, // clear the depth data
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, // don't care after rendering
		.clearValue{.depthStencil{1.0f, 0}}
	};
	VkRenderingInfo renderingInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea
		{
			.offset{.x = 0, .y = 0},
			.extent{.width = m_swapchainWidth, .height = m_swapchainHeight}
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachInfo,
		.pDepthAttachment = &depthAttachInfo
	};

	// setup frame data
	vkCmdBindDescriptorSets(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_globalDescSet, 0, nullptr);

	FrameConstants frameConsts;
	GPUBuffer &vertBuffer = m_buffers[m_vertexBufferId - 1];
	GPUBuffer &materialBuffer = m_buffers[m_matBufferId - 1];
	frameConsts.vertexBufferAddress = vertBuffer.deviceAddress;
	frameConsts.materialBufferAddress = materialBuffer.deviceAddress;
	frameConsts.renderItemsAddress = res.renderItemBuffer.deviceAddress;
	vkCmdPushConstants(res.commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(FrameConstants), &frameConsts);

	GPUBuffer &idxBuffer = m_buffers[m_indexBufferId - 1];
	vkCmdBindIndexBuffer(res.commandBuffer, idxBuffer.vkBuffer, 0, VK_INDEX_TYPE_UINT32);

	// begin dynamic rendering
	vkCmdBeginRendering(res.commandBuffer, &renderingInfo);
	{
		// set the viewpot and scissor state
		VkViewport viewport
		{
			.x = 0,
			.y = static_cast<float>(m_swapchainHeight),
			.width = static_cast<float>(m_swapchainWidth),
			.height = -static_cast<float>(m_swapchainHeight),
			.minDepth = 0, .maxDepth = 1
		};
		vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);

		VkRect2D scissor
		{
			.offset{.x = 0, .y = 0 },
			.extent{.width = m_swapchainWidth, .height = m_swapchainHeight}
		};
		vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);
		vkCmdBindPipeline(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdDrawIndexedIndirect(res.commandBuffer, res.indirectDrawBuffer.vkBuffer, 0, drawIndex, sizeof(VkDrawIndexedIndirectCommand));
	}
	// end dynamic rendering
	vkCmdEndRendering(res.commandBuffer);

	// transition the image from color attachment to presentation so we can show it
	VkImageMemoryBarrier2 presentLayoutBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_NONE, // nothing is waiting, but the cache is flushed and layout is transition
		.dstAccessMask = 0,
		.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.image = m_swapchainImages[imageIndex],
		.subresourceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	};
	VkDependencyInfo presentDepInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &presentLayoutBarrier
	};
	vkCmdPipelineBarrier2(res.commandBuffer, &presentDepInfo);

	vkEndCommandBuffer(res.commandBuffer);

	// ensure swapchain image is actually vailable to start color output
	VkSemaphoreSubmitInfo imageAcquireWaitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = imageAcquireSemaphore,
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT // wait before drawing to image
	};
	// signal that the image can be presented
	std::array<VkSemaphoreSubmitInfo, 2> semaphoreSignals
	{
		VkSemaphoreSubmitInfo
		{ // render work completion signal
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_renderCompleteSemaphores[imageIndex],
			.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
		},
		VkSemaphoreSubmitInfo
		{ // entire frame is completed (timeline)
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_timelineSemaphore,
			.value = signalValue,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		}
	};
	VkCommandBufferSubmitInfo cmdSubmitInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = res.commandBuffer,
	};
	VkSubmitInfo2 submitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &imageAcquireWaitInfo, // ensure the image is ready
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdSubmitInfo,
		.signalSemaphoreInfoCount = static_cast<uint32_t>(semaphoreSignals.size()),
		.pSignalSemaphoreInfos = semaphoreSignals.data()
	};
	vkQueueSubmit2(m_gfxQueue, 1, &submitInfo, VK_NULL_HANDLE);

	// present the image
	VkPresentInfoKHR presentInfo{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &m_renderCompleteSemaphores[imageIndex], // render work completed semaphore
		.swapchainCount = 1,
		.pSwapchains = &m_swapchain,
		.pImageIndices = &imageIndex,
		.pResults = nullptr
	};

	vkQueuePresentKHR(m_gfxQueue, &presentInfo);
}

std::pair<uint32_t, GPUBuffer> Application::createImage(VkCommandBuffer commandBuffer, unsigned char *imageData, uint32_t width, uint32_t height, int channels)
{
	// create vk image and allocation
	VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;
	VkImageCreateInfo imageInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = imageFormat,
		.extent {.width = width, .height = height, .depth = 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	VmaAllocationCreateInfo allocInfo{ .usage = VMA_MEMORY_USAGE_AUTO };
	GPUImage gpuImage;
	if (vmaCreateImage(m_vmaAllocator, &imageInfo, &allocInfo, &gpuImage.image, &gpuImage.allocation, nullptr) != VK_SUCCESS)
	{
		showError("Error creating image");
		return { 0, GPUBuffer{} };
	}

	VkImageViewCreateInfo imgViewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = gpuImage.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = imageFormat,
		.subresourceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1
		}
	};
	if (vkCreateImageView(m_device, &imgViewInfo, nullptr, &gpuImage.imageView) != VK_SUCCESS)
	{
		showError("Error creating image view");
		return { 0, GPUBuffer{} };
	}

	// transition the image to transfer-DST
	VkImageMemoryBarrier2 transferBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
		.srcAccessMask = VK_ACCESS_2_NONE,
		.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
		.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.image = gpuImage.image,
		.subresourceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	};
	VkDependencyInfo transferDepInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &transferBarrier
	};
	vkCmdPipelineBarrier2(commandBuffer, &transferDepInfo);

	// create staging buffer and issue record copy operation
	const size_t byteSize = width * height * channels;
	GPUBuffer stageBuff = createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, byteSize, true, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
	mapCopyBufferData(stageBuff, 0, imageData, byteSize);

	VkBufferImageCopy buffImgCopy
	{
		.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
		.imageExtent = {.width = width, .height = height, .depth = 1 },
	};
	vkCmdCopyBufferToImage(commandBuffer, stageBuff.vkBuffer, gpuImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffImgCopy);

	// transition image for shader read/sampling
	VkImageMemoryBarrier2 shaderReadBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
		.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.image = gpuImage.image,
		.subresourceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	};
	VkDependencyInfo shaderReadDepInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &shaderReadBarrier
	};
	vkCmdPipelineBarrier2(commandBuffer, &shaderReadDepInfo);

	m_images.push_back(gpuImage);
	const uint32_t imageId = m_images.size();
	return { imageId, stageBuff };
	/*
	// need different image usage for host to gpu copy
	VmaAllocationCreateInfo allocInfo{ .usage = VMA_MEMORY_USAGE_CPU_TO_GPU };
	GPUImage gpuImage;
	if (vmaCreateImage(m_vmaAllocator, &imageInfo, &allocInfo, &gpuImage.image, &gpuImage.allocation, nullptr) != VK_SUCCESS)
	{
		showError("Error creating image");
		return { 0, GPUBuffer{} };
	}

	// ensure the image is in shader-read-optimal layout
	VkHostImageLayoutTransitionInfo transition
	{
		.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO,
		.image = texture.image,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
	};
	if (!vkTransitionImageLayout(m_device, 1, &transition))
	{
		showError("Error transitioning GPU image");
		return 0;
	}
	// copy the image data
	VkMemoryToImageCopy memCopy
	{
		.sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY,
		.pHostPointer = imageData,
		.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
		.imageExtent = { .width = width, .height = height, .depth = 1 },
	};
	VkCopyMemoryToImageInfo copyInfo
	{
		.sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY,
		.dstImage = texture.image,
		.dstImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.regionCount = 1,
		.pRegions = &memCopy
	};
	if (vkCopyMemoryToImage(m_device, &copyInfo) != VK_SUCCESS)
	{
		showError("Error copying image data");
		return 0;
	}
	*/
}

void Application::updateTextureDescriptors() const
{
	// create combined image & sampler descriptor writes for all textures
	std::vector<VkDescriptorImageInfo> imageDescriptors;
	imageDescriptors.reserve(m_textures.size());
	for (const Texture &texture : m_textures)
	{
		imageDescriptors.push_back({
			.sampler = m_samplers[texture.samplerId - 1],
			.imageView = m_images[texture.imageId - 1].imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	}
	VkWriteDescriptorSet descSetWrite{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
								.dstSet = m_globalDescSet,
								.dstBinding = 0,
								.dstArrayElement = 0,
								.descriptorCount = static_cast<uint32_t>(imageDescriptors.size()),
								.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
								.pImageInfo = imageDescriptors.data() };
	vkUpdateDescriptorSets(m_device, 1, &descSetWrite, 0, nullptr);
}

std::vector<uint32_t> Application::loadMaterials(const tg3_model &model, const std::vector<uint32_t> &textureIds)
{
	std::vector<uint32_t> materialIds(model.materials_count);
	for (int i = 0; i < model.materials_count; ++i)
	{
		const tg3_material *tg3mat = &model.materials[i];
		m_materials.push_back(Material
			{
				.baseColor = glm::vec4(
					tg3mat->pbr_metallic_roughness.base_color_factor[0],
					tg3mat->pbr_metallic_roughness.base_color_factor[1],
					tg3mat->pbr_metallic_roughness.base_color_factor[2],
					tg3mat->pbr_metallic_roughness.base_color_factor[3]),
				.textureIndex = tg3mat->pbr_metallic_roughness.base_color_texture.index != -1
					? textureIds[tg3mat->pbr_metallic_roughness.base_color_texture.index] - 1
					: 0
			});
		materialIds[i] = m_materials.size();
	}
	return materialIds;
}

std::vector<uint32_t> Application::loadMeshes(const tg3_model &model, const std::vector<uint32_t> &materialIds)
{
	std::vector<uint32_t> meshIds(model.meshes_count);
	// load all gltf mesh and primitive data
	for (int i = 0; i < model.meshes_count; ++i)
	{
		Mesh mesh;
		const tg3_mesh *tg3mesh = &model.meshes[i];
		mesh.name = tg3mesh->name.data != nullptr ? tg3mesh->name.data : "No Name";

		// lambda to perform attribute data copy
		auto writeAttribute = [this, &model]<typename T>(T Vertex:: * member, const tg3_str_int_pair * attr)
		{
			const tg3_accessor *accessor = &model.accessors[attr->value];
			const tg3_buffer_view *bufferView = &model.buffer_views[accessor->buffer_view];
			const tg3_buffer *buffer = &model.buffers[bufferView->buffer];
			const size_t bufferOffset = bufferView->byte_offset + accessor->byte_offset;
			const size_t stride = bufferView->byte_stride != 0 ? bufferView->byte_stride : sizeof(T);

			for (uint64_t idx = 0; idx < accessor->count; ++idx)
			{
				const size_t elementOffset = bufferOffset + idx * stride;
				const float *data = reinterpret_cast<const float *>(buffer->data.data + elementOffset);
				if constexpr (std::is_same<T, glm::vec3>())
				{
					m_vertices[m_vertOffset + idx].*member = glm::vec3(data[0], data[1], data[2]);
				}
				else if constexpr (std::is_same<T, glm::vec2>())
				{
					m_vertices[m_vertOffset + idx].*member = glm::vec2(data[0], data[1]);
				}
			}
		};

		// copy the vertex data
		mesh.subMeshes.resize(tg3mesh->primitives_count);
		for (int s = 0; s < tg3mesh->primitives_count; ++s)
		{
			const tg3_primitive *primitive = &tg3mesh->primitives[s];
			mesh.subMeshes[s].materialId = materialIds[primitive->material];
			mesh.subMeshes[s].vertexStart = m_vertOffset;

			for (int a = 0; a < primitive->attributes_count; ++a)
			{
				const tg3_str_int_pair *attr = &primitive->attributes[a];
				if (strcmp(attr->key.data, "POSITION") == 0)
				{
					const tg3_accessor *accessor = &model.accessors[attr->value];
					assert(accessor->type == TG3_TYPE_VEC3 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT);
					assert(m_vertOffset + accessor->count <= m_vertices.size() && "Not enough space to load vertices");

					mesh.subMeshes[s].vertexCount = accessor->count;
					writeAttribute(&Vertex::position, attr);
				}
				else if (strcmp(attr->key.data, "NORMAL") == 0)
				{
					const tg3_accessor *accessor = &model.accessors[attr->value];
					assert(accessor->type == TG3_TYPE_VEC3 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT);
					writeAttribute(&Vertex::normal, attr);
				}
				else if (strcmp(attr->key.data, "COLOR_0") == 0)
				{
					const tg3_accessor *accessor = &model.accessors[attr->value];
					assert(accessor->type == TG3_TYPE_VEC3 || accessor->type == TG3_TYPE_VEC4);
					assert(accessor->component_type == TG3_COMPONENT_TYPE_FLOAT);
					writeAttribute(&Vertex::color, attr);
				}
				else if (strcmp(attr->key.data, "TEXCOORD_0") == 0)
				{
					const tg3_accessor *accessor = &model.accessors[attr->value];
					assert(accessor->type == TG3_TYPE_VEC2 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT);
					writeAttribute(&Vertex::uv, attr);
				}
			}
			m_vertOffset += mesh.subMeshes[s].vertexCount;

			// copy index data
			if (primitive->indices != -1)
			{
				const tg3_accessor *accessor = &model.accessors[primitive->indices];
				const tg3_buffer_view *bufferView = &model.buffer_views[accessor->buffer_view];
				const tg3_buffer *buffer = &model.buffers[bufferView->buffer];
				assert(m_idxOffset + accessor->count <= m_indices.size() && "Not enough space for indices");

				mesh.subMeshes[s].indexStart = m_idxOffset;
				mesh.subMeshes[s].indexCount = accessor->count;

				if (accessor->component_type == TG3_COMPONENT_TYPE_UNSIGNED_INT)
				{
					const uint32_t *buffData = reinterpret_cast<const uint32_t *>(buffer->data.data + bufferView->byte_offset + accessor->byte_offset);
					memcpy(&m_indices[m_idxOffset], buffData, accessor->count * sizeof(uint32_t));
				}
				else if (accessor->component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT)
				{
					const uint16_t *buffData = reinterpret_cast<const uint16_t *>(buffer->data.data + bufferView->byte_offset + accessor->byte_offset);
					for (uint64_t idx = 0; idx < accessor->count; ++idx)
					{
						m_indices[m_idxOffset + idx] = static_cast<uint32_t>(buffData[idx]);
					}
				}
				m_idxOffset += mesh.subMeshes[s].indexCount;
			}
		}
		m_meshes.push_back(std::move(mesh));
		meshIds[i] = m_meshes.size();
	}
	return meshIds;
}

bool Application::createDescriptorSets()
{
	std::array<VkDescriptorPoolSize, 1> poolSizes
	{
		VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = MaxTextures }
	};

	VkDescriptorPoolCreateInfo poolInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = 1,
		.poolSizeCount = poolSizes.size(),
		.pPoolSizes = poolSizes.data()
	};
	if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descPool) != VK_SUCCESS)
	{
		showError("Unable to create descriptor pool");
		return false;
	}

	// global descriptor set
	std::array<VkDescriptorSetLayoutBinding, 1> bindings
	{
		VkDescriptorSetLayoutBinding
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MaxTextures,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		}
	};
	std::array<VkDescriptorBindingFlags, 1> flags;
	flags[0] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

	VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = flags.size(),
		.pBindingFlags = flags.data()
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &flagsInfo,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = bindings.size(),
		.pBindings = bindings.data()
	};
	if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_globalDSLayout) != VK_SUCCESS)
	{
		showError("Unable to create descriptor set layout");
		return false;
	}

	// create the actual descriptor sets
	VkDescriptorSetAllocateInfo descSetAllocInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = m_descPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &m_globalDSLayout,
	};
	if (vkAllocateDescriptorSets(m_device, &descSetAllocInfo, &m_globalDescSet) != VK_SUCCESS)
	{
		showError("Unable to allocate descriptor set");
		return false;
	}
	return true;
}

bool Application::createIndirectDrawBuffers()
{
	for (auto &res : m_frameResources)
	{
		// indirect drawing buffer
		const size_t indirectBuffByteSize = m_nodeWorld.maxNodes() * sizeof(VkDrawIndexedIndirectCommand);
		res.indirectDrawBuffer = createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, indirectBuffByteSize, true, VMA_MEMORY_USAGE_AUTO);

		// map indirect draw buffer
		void *indBuffPtr = nullptr;
		if (vmaMapMemory(m_vmaAllocator, res.indirectDrawBuffer.allocation, &indBuffPtr) != VK_SUCCESS)
		{
			showError("Unable to map indirect draw buffer");
			return false;
		}
		res.indirectDrawPtr = reinterpret_cast<VkDrawIndexedIndirectCommand *>(indBuffPtr);

		// render item buffer (per-draw data)
		const size_t renderItemByteSize = m_nodeWorld.maxNodes() * sizeof(RenderItem);
		res.renderItemBuffer = createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, renderItemByteSize, true, VMA_MEMORY_USAGE_AUTO);

		// map indirect draw buffer
		void *riBuffPtr = nullptr;
		if (vmaMapMemory(m_vmaAllocator, res.renderItemBuffer.allocation, &riBuffPtr) != VK_SUCCESS)
		{
			showError("Unable to map render item buffer");
			return false;
		}
		res.renderItemPtr = reinterpret_cast<RenderItem *>(riBuffPtr);
	}
	return true;
}

void Application::updateProjectionMatrix()
{
	const float aspectRatio = m_width / static_cast<float>(m_height);
	m_matProj = glm::perspectiveRH(glm::radians(65.0f), aspectRatio, 0.01f, 1000.0f);
}

void Application::updateViewMatrix()
{
	m_camera.yaw -= glm::radians(m_mouseXRel * m_mouseSensitivity);
	m_camera.pitch -= glm::radians(m_mouseYRel * m_mouseSensitivity);
	m_camera.pitch = glm::clamp(m_camera.pitch, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
	m_mouseXRel = 0;
	m_mouseYRel = 0;

	// camera and view matrix
	Node &camNode = m_nodeWorld.getNode(m_cameraNodeId);
	glm::vec3 camPosition = camNode.getTranslation();
	if (m_camera.type == CameraType::orbit)
	{
		camPosition = glm::vec3(cosf(m_camera.yaw) * cosf(m_camera.pitch), sinf(m_camera.pitch), sinf(m_camera.yaw) * cosf(m_camera.pitch)) * m_camera.distance;
		camNode.setTranslation(camPosition);
		m_matView = glm::lookAtRH(camPosition, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
	}
	else if (m_camera.type == CameraType::firstPerson)
	{
		glm::quat cameraQuat = glm::quat(glm::vec3(m_camera.pitch, m_camera.yaw, 0.0f));
		camNode.setRotation(cameraQuat);
		const bool is6DegFree = false;
		m_camera.forward = glm::normalize(cameraQuat * glm::vec3(0, 0, -1));
		m_camera.right = glm::normalize(glm::cross(m_camera.forward, is6DegFree ? m_camera.up : glm::vec3(0, 1, 0)));
		m_camera.up = glm::normalize(glm::cross(m_camera.right, m_camera.forward));
		m_matView = glm::lookAtRH(camPosition, camPosition + m_camera.forward, m_camera.up);

		glm::quat movementQuat = glm::quat(glm::vec3(is6DegFree ? m_camera.pitch : 0.0f, m_camera.yaw, 0.0f));
		m_moveDirection = movementQuat * glm::vec3(0, 0, -1);
	}
	m_viewProjMatrix = m_matProj * m_matView;
}

GPUBuffer Application::createBuffer(VkBufferUsageFlags usage, size_t byteSize, bool mappable, VmaMemoryUsage memoryUsage)
{
	// create buffer and vma allocation
	VkBufferCreateInfo buffInfo
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = byteSize,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};
	VmaAllocationCreateInfo allocInfo
	{
		.flags = mappable ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u,
		.usage = memoryUsage,
	};
	GPUBuffer gpuBuff;
	if (vmaCreateBuffer(m_vmaAllocator, &buffInfo, &allocInfo, &gpuBuff.vkBuffer, &gpuBuff.allocation, nullptr) != VK_SUCCESS)
	{
		return GPUBuffer{};
	}

	// BDA Send Device Pointer
	if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	{
		VkBufferDeviceAddressInfo vertBdaInfo
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = gpuBuff.vkBuffer
		};
		gpuBuff.deviceAddress = vkGetBufferDeviceAddress(m_device, &vertBdaInfo);
	}
	return gpuBuff;
}

void Application::mapCopyBufferData(const GPUBuffer &buffer, size_t bufferOffset, void *data, size_t byteSize)
{
	// map and write buffer data
	void *buffPtr = nullptr;
	if (vmaMapMemory(m_vmaAllocator, buffer.allocation, &buffPtr) != VK_SUCCESS)
	{
		showError("Unable to map buffer memory");
		return;
	}
	std::memcpy(static_cast<char *>(buffPtr) + bufferOffset, data, byteSize);
	vmaUnmapMemory(m_vmaAllocator, buffer.allocation);
}

uint32_t Application::addBuffer(const GPUBuffer &buffer)
{
	m_buffers.push_back(buffer);
	uint32_t bufferId = m_buffers.size();
	return bufferId;
}

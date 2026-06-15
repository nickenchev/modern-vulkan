#include "application.h"
#include "utils.h"

#include <SDL3/SDL.h>
#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <iostream>
#include "gltfloader.h"

void Application::showError(const std::string &errorMessasge) const
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessasge.c_str(), window);
}

bool Application::initialize()
{
	SDL_InitSubSystem(SDL_INIT_VIDEO);
	window = SDL_CreateWindow("Vulkan Learning", width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if (!window)
	{
		showError("Error creating window");
		return false;
	}

	if (!initializeVulkan())
	{
		return false;
	}

	return true;
}

bool Application::loadData()
{
	std::string filePath = "D:\\glTF-Sample-Models\\2.0\\DamagedHelmet\\glTF\\DamagedHelmet.gltf";
	tg3_model model;
	if (!parseModel(filePath, model))
	{
		return false;
	}

	// imported images, materials, meshes, etc
	ImportedResources importedRes;
	importResources(filePath, model, importedRes);

	VkCommandBuffer commandBuffer = startTransientCommandBuffer();
	std::vector<GPUBuffer> stagingBuffers;
	stagingBuffers.reserve(importedRes.images.size() + 1);

	// create a fallback color texture
	uint32_t whitePixel = 0xFFFFFFFF; // RGBA
	auto [ whiteTexId, whiteTexBuffer ] = createTexture(commandBuffer, reinterpret_cast<unsigned char *>(&whitePixel), 1, 1, 4);
	stagingBuffers.push_back(whiteTexBuffer);

	// upload images to GPU textures
	std::vector<uint32_t> textureIds(importedRes.images.size());
	for (int i = 0; i < importedRes.images.size(); ++i)
	{
		Image &img = importedRes.images[i];
		auto [ textureId, stagingTexBuffer ] = createTexture(commandBuffer, img.data, img.width, img.height, img.channels);
		textureIds[i] = textureId;
		stagingBuffers.push_back(stagingTexBuffer);
	}

	submitTransientCommandBuffer(commandBuffer); // submit and wait

	// clean up the staging buffers
	for (GPUBuffer &stageBuff : stagingBuffers)
	{
		vmaDestroyBuffer(vmaAllocator, stageBuff.vkBuffer, stageBuff.allocation);
	}

	VkSamplerCreateInfo samplerInfo{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
									.magFilter = VK_FILTER_LINEAR,
									.minFilter = VK_FILTER_LINEAR,
									.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
									.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
									.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
									.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
									.anisotropyEnable = VK_TRUE,
									.maxAnisotropy = 16.0f, // Check limits
									.compareEnable = VK_FALSE,
									.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
									.unnormalizedCoordinates = VK_FALSE };

	if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
	{
		showError("Unable to create texture sampler");
		return false;
	}

	// update the texture descriptors
	std::vector<VkDescriptorImageInfo> descriptorWrites(textures.size());
	for (GPUTexture &texture : textures)
	{
		descriptorWrites.push_back({
			.sampler = sampler,
			.imageView = texture.imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		});
	}

	VkWriteDescriptorSet descWrites{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
								.dstSet = globalDescSet,
								.dstBinding = 0,
								.dstArrayElement = 0,
								.descriptorCount = static_cast<uint32_t>(descriptorWrites.size()),
								.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
								.pImageInfo = descriptorWrites.data() };
	vkUpdateDescriptorSets(device, 1, &descWrites, 0, nullptr);

	// create GPU side material list
	std::vector<uint32_t> materialIds(importedRes.materials.size());
	for (int i = 0; i < importedRes.materials.size(); ++i)
	{
		const Material &mat = importedRes.materials[i];
		GPUMaterial gpuMat;
		gpuMat.baseColor = mat.baseColor;
		gpuMat.textureId = textureIds[mat.baseColorTextureIndex];
		materialIds[i] = createMaterial(std::move(gpuMat));
	}

	// upload geo data and get buffer Ids
	const size_t vertBufferSize = importedRes.vertices.size() * sizeof(Vertex);
	GPUBuffer vertexBuffer = createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, vertBufferSize, importedRes.vertices.data());
	if (!vertexBuffer.vkBuffer)
	{
		showError("Error creating vertex buffer");
		tg3_model_free(&model);
		return false;
	}
	uint32_t vertexBufferId = addBuffer(vertexBuffer);

	const size_t indexBufferSize = importedRes.indices.size() * sizeof(uint32_t);
	GPUBuffer indexBuffer = createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, indexBufferSize, importedRes.indices.data());
	if (!indexBuffer.vkBuffer)
	{
		showError("Error creating index buffer");
		tg3_model_free(&model);
		return false;
	}
	uint32_t indexBufferId = addBuffer(indexBuffer);

	// set buffer Ids and generate meshIds
	std::vector<uint32_t> meshIds(importedRes.meshes.size());
	for (int i = 0; i < importedRes.meshes.size(); ++i)
	{
		Mesh &mesh = importedRes.meshes[i];
		mesh.vertexBufferId = vertexBufferId;
		mesh.indexBufferId = indexBufferId;

		for (SubMesh &subMesh : mesh.subMeshes)
		{
			subMesh.materialId = materialIds[subMesh.materialId];
		}
		meshIds[i] = addMesh(std::move(mesh));
	}

	// import scene nodes
	const tg3_scene *scene = &model.scenes[model.default_scene != -1
		? model.default_scene
		: 0];

	uint32_t lastNodeId = 0;
	for (int i = 0; i < scene->nodes_count; ++i)
	{
		lastNodeId = importNode(nodeWorld, model, scene->nodes[i], 0, lastNodeId, meshIds);
	}

	tg3_model_free(&model);

	return true;
}

VkCommandBuffer Application::startTransientCommandBuffer()
{
	// allocate the transient command buffer
	VkCommandBufferAllocateInfo cmdAllocInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	VkCommandBuffer commandBuffer = nullptr;
	if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer) != VK_SUCCESS)
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

	vkQueueSubmit(gfxQueue, 1, &submitInfo, nullptr);
	vkQueueWaitIdle(gfxQueue);
	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void Application::shutdown()
{
	// wait in case resources are in use
	vkDeviceWaitIdle(device);

	// clean up descriptor layouts and pool
	if (globalDSLayout)
	{
		vkDestroyDescriptorSetLayout(device, globalDSLayout, nullptr);
	}
	if (frameDSLayout)
	{
		vkDestroyDescriptorSetLayout(device, frameDSLayout, nullptr);
	}
	if (descPool)
	{
		vkDestroyDescriptorPool(device, descPool, nullptr);
	}

	// delete textures
	for (auto &tex : textures)
	{
		vkDestroyImageView(device, tex.imageView, nullptr);
		vkDestroyImage(device, tex.image, nullptr);
		vmaFreeMemory(vmaAllocator, tex.allocation);
	}

	// delete buffers
	for (auto &buff : buffers)
	{
		vkDestroyBuffer(device, buff.vkBuffer, nullptr);
		vmaFreeMemory(vmaAllocator, buff.allocation);
	}

	// frame / sync object cleanup
	if (timelineSemaphore)
	{
		vkDestroySemaphore(device, timelineSemaphore, nullptr);
	}
	for (auto &res : frameResources)
	{
		vkDestroySemaphore(device, res.imageAcquiredSemaphore, nullptr);
		vkDestroyCommandPool(device, res.commandPool, nullptr); // destroys buffers implicitly
	}

	vkDestroyCommandPool(device, commandPool, nullptr);

	// pipeline cleanup
	if (pipelineLayout)
	{
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	}
	if (pipeline)
	{
		vkDestroyPipeline(device, pipeline, nullptr);
	}

	// cleanup shaders
	if (vertShader)
	{
		vkDestroyShaderModule(device, vertShader, nullptr);
	}
	if (fragShader)
	{
		vkDestroyShaderModule(device, fragShader, nullptr);
	}

	// cleanup swapchain
	destroySwapchain();

	// VMA
	if (vmaAllocator)
	{
		vmaDestroyAllocator(vmaAllocator);
	}

	// cleanup Vulkan
	if (surface)
	{
		vkDestroySurfaceKHR(vulkanInstance, surface, nullptr);
	}
	if (device)
	{
		vkDestroyDevice(device, nullptr);
	}
	if (vulkanInstance)
	{
		vkDestroyInstance(vulkanInstance, nullptr);
	}
	volkFinalize();

	// cleanup SDL
	if (window)
	{
		SDL_DestroyWindow(window);
	}
	SDL_Quit();
}

void Application::run()
{
	running = true;
	while (running)
	{
		SDL_Event event{ 0 };
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT)
			{
				running = false;
				break;
			}
			else if (event.type == SDL_EVENT_WINDOW_RESIZED)
			{
				width = event.window.data1;
				height = event.window.data2;
				break;
			}
		}

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

	if (physicalDevice = findPhysicalDevice(); !physicalDevice)
	{
		showError("Unable to find an appropriate physical device");
		return false;
	}

	if (!findGraphicsQueue())
	{
		showError("Unable to find a compatible graphics queue");
		return false;
	}

	if (!createDevice(physicalDevice))
	{
		showError("Couldn't create the logical GPU device");
		return false;
	}

	if (!initializeVMA())
	{
		showError("Unable to create Vulkan Memory Allocator");
		return false;
	}

	if (!createSwapchain(width, height))
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

	if (pipeline = createGraphicsPipeline(); !pipeline)
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

	if (vkCreateInstance(&instCreateInfo, nullptr, &vulkanInstance) != VK_SUCCESS)
	{
		return false;
	}

	volkLoadInstance(vulkanInstance);
	return true;
}

bool Application::createSurface()
{
	if (!SDL_Vulkan_CreateSurface(window, vulkanInstance, nullptr, &surface))
	{
		return false;
	}
	return true;
}

VkPhysicalDevice Application::findPhysicalDevice()
{
	// enumerate all physical devices
	uint32_t physDeviceCount = 0;
	vkEnumeratePhysicalDevices(vulkanInstance, &physDeviceCount, nullptr);
	std::vector<VkPhysicalDevice> physicalDevices(physDeviceCount);
	vkEnumeratePhysicalDevices(vulkanInstance, &physDeviceCount, physicalDevices.data());

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
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());

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
	vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamCount, nullptr);
	std::vector<VkQueueFamilyProperties2> queueFamProps(queueFamCount, { VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
	vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamCount, queueFamProps.data());

	for (int currentFamIdx = 0; currentFamIdx < queueFamProps.size(); currentFamIdx++)
	{
		// ensure it has presentation support
		VkBool32 hasPresentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, currentFamIdx, surface, &hasPresentSupport);

		const auto &props = queueFamProps[currentFamIdx];
		// ensure this is a GRAPHICS queue with presentation support
		if (props.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT && hasPresentSupport)
		{
			gfxQueueFamIdx = currentFamIdx;
			return true;
		}
	}
	return false;
}


bool Application::createDevice(VkPhysicalDevice physicalDevice)
{
	float queuePriority = 1.0f;
	std::vector<uint32_t> queueFamiles{ gfxQueueFamIdx };

	VkDeviceQueueCreateInfo gfxQueueInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = gfxQueueFamIdx,
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
		!supportedFeatures.features.shaderInt64)
	{
		showError("Physical device doesn't meet the feature requirements");
		return false;
	}

	// produce a separate features struct chain for device creation
	VkPhysicalDeviceVulkan14Features features14
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = nullptr,
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
		.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
		.descriptorBindingPartiallyBound = VK_TRUE,
		.runtimeDescriptorArray = VK_TRUE,
		.scalarBlockLayout = VK_TRUE,
		.timelineSemaphore = VK_TRUE,
		.bufferDeviceAddress = VK_TRUE
	};
	VkPhysicalDeviceFeatures2 features
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features12,
		.features
		{
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

	if (vkCreateDevice(physicalDevice, &devCreateInfo, nullptr, &device) != VK_SUCCESS)
	{
		return false;
	}

	// grab the VkQueue object finally
	vkGetDeviceQueue(device, gfxQueueFamIdx, 0, &gfxQueue);
	if (!gfxQueue)
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
		.physicalDevice = physicalDevice,
		.device = device,
		.pVulkanFunctions = &vmaFuncInfo,
		.instance = vulkanInstance,
		.vulkanApiVersion = VulkanVersion
	};

	// vma can import directly from volk
	vmaImportVulkanFunctionsFromVolk(&vmaAllocInfo, &vmaFuncInfo);

	if (vmaCreateAllocator(&vmaAllocInfo, &vmaAllocator) != VK_SUCCESS)
	{
		return false;
	}
	return true;
}

bool Application::createSwapchain(uint32_t width, uint32_t height)
{
	swapchainWidth = width;
	swapchainHeight = height;

	VkSurfaceCapabilitiesKHR surfaceCaps{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps) != VK_SUCCESS)
	{
		showError("Couldn't get the surface capabilities");
		return false;
	}

	VkSwapchainCreateInfoKHR swapchainCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = surfaceCaps.minImageCount,
		.imageFormat = SwapchainFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent{.width = swapchainWidth, .height = swapchainHeight },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR
	};

	if (vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain) != VK_SUCCESS)
	{
		showError("Error creating swapchain");
		return false;
	}

	// grab the swapchain images
	uint32_t imageCount = 0;
	vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
	swapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
	swapchainImageViews.resize(imageCount);

	// create the swapchain image views
	for (size_t i = 0; i < swapchainImages.size(); ++i)
	{
		VkImageViewCreateInfo imgViewInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = SwapchainFormat,
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.levelCount = 1,
				.layerCount = 1
			}
		};

		if (vkCreateImageView(device, &imgViewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS)
		{
			showError("Error creating swapchain image view");
			return false;
		}
	}

	// semaphores used to signal render completion
	renderCompleteSemaphores.resize(swapchainImages.size());
	for (VkSemaphore &semaphore : renderCompleteSemaphores)
	{
		VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
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
		.extent{.width = swapchainWidth, .height = swapchainHeight, .depth = 1 },
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
	if (vmaCreateImage(vmaAllocator, &depthCreateInfo, &allocInfo, &depthImage, &depthImageAllocation, nullptr) != VK_SUCCESS)
	{
		showError("Error allocating depth image");
		return false;
	}

	VkImageViewCreateInfo depthImgViewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = depthImage,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = DepthFormat,
		.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1}
	};
	if (vkCreateImageView(device, &depthImgViewInfo, nullptr, &depthImageView) != VK_SUCCESS)
	{
		showError("Error creating depth image view");
		return false;
	}

	return true;
}

void Application::destroySwapchain()
{
	for (VkImageView swapchainImgView : swapchainImageViews)
	{
		vkDestroyImageView(device, swapchainImgView, nullptr);
	}
	swapchainImageViews.clear();

	// destroy render-complete ssemaphores
	for (VkSemaphore &semaphore : renderCompleteSemaphores)
	{
		vkDestroySemaphore(device, semaphore, nullptr);
	}
	renderCompleteSemaphores.clear();

	if (swapchain)
	{
		vkDestroySwapchainKHR(device, swapchain, nullptr);
		swapchain = nullptr;
	}

	// destroy the depth buffer along with the swapchain
	if (depthImageView)
	{
		vkDestroyImageView(device, depthImageView, nullptr);
		vmaDestroyImage(vmaAllocator, depthImage, depthImageAllocation);
		depthImageView = nullptr;
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
	if (vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		showError("Error creating shader module");
		return nullptr;
	}
	return shaderModule;
}

bool Application::createShaders()
{
	// create the shader modules that we'll need for the graphics pipeline
	if (vertShader = createShaderModule("shader.vert", shaderc_vertex_shader); !vertShader)
	{
		return false;
	}
	if (fragShader = createShaderModule("shader.frag", shaderc_fragment_shader); !fragShader)
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
									   .size = sizeof(DrawConstants) };

	std::array<VkDescriptorSetLayout, 2> dsLayouts{ globalDSLayout, frameDSLayout };

	VkPipelineLayoutCreateInfo pipelineLayoutInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = dsLayouts.size(),
		.pSetLayouts = dsLayouts.data(),
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstRange
	};

	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
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
			.module = vertShader,
			.pName = entryPoint
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragShader,
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
		.layout = pipelineLayout,
		.renderPass = VK_NULL_HANDLE,
	};
	if (vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
	{
		showError("Error creating the pipeline");
		return nullptr;
	}
	return pipeline;
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
	if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &timelineSemaphore) != VK_SUCCESS)
	{
		showError("Unable to create the timeline semaphore");
		return false;
	}

	// per-frame image-acquire semaphores
	for (FrameResources &res : frameResources)
	{
		// create the binary semaphores
		VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &res.imageAcquiredSemaphore) != VK_SUCCESS)
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
		.queueFamilyIndex = gfxQueueFamIdx
	};
	if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
	{
		showError("Unable to create command buffer pool");
		return false;
	}

	for (FrameResources &res : frameResources)
	{
		// we'll give each frame its own pool, faster cmd buffer resets this way
		VkCommandPoolCreateInfo poolInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = gfxQueueFamIdx
		};
		if (vkCreateCommandPool(device, &poolInfo, nullptr, &res.commandPool) != VK_SUCCESS)
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

		if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &res.commandBuffer) != VK_SUCCESS)
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
	if (requireSwapchainRecreate)
	{
		vkDeviceWaitIdle(device);
		destroySwapchain();
		createSwapchain(width, height);
		requireSwapchainRecreate = false;
	}

	const uint32_t frameResIndex = frameIndex++ % MaxFramesInFlight;
	const uint64_t signalValue = nextSignalValue++;
	const uint64_t waitValue = signalValue - MaxFramesInFlight;

	VkSemaphoreWaitInfo waitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &timelineSemaphore,
		.pValues = &waitValue
	};
	vkWaitSemaphores(device, &waitInfo, UINT64_MAX);

	// now its safe to start recording commands
	FrameResources &res = frameResources[frameResIndex];
	vkResetCommandPool(device, res.commandPool, 0);

	// get the resources for this frame
	VkSemaphore imageAcquireSemaphore = frameResources[frameResIndex].imageAcquiredSemaphore;

	uint32_t imageIndex = 0;
	VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAcquireSemaphore, VK_NULL_HANDLE, &imageIndex);

	// handle resize and out-of-date images, may need swapchain recreate
	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		requireSwapchainRecreate = true;
		return;
	}
	else if (acquireResult == VK_SUBOPTIMAL_KHR)
	{
		// can render this frame, recreate next time around
		requireSwapchainRecreate = true;
	}

	// begin recording commands
	VkCommandBufferBeginInfo cmdBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	vkBeginCommandBuffer(res.commandBuffer, &cmdBeginInfo);

	// transition the color and depth images
	std::vector<VkImageMemoryBarrier2> layoutBarriers
	{
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.image = swapchainImages[imageIndex],
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		},
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, // both specified to control memory access at both stages (write)
			.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.image = depthImage,
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
		.imageView = swapchainImageViews[imageIndex],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, // clear the image
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE, // keep data for presentation
		.clearValue{.color{0.01f, 0.01f, 0.01f, 1}}
	};
	VkRenderingAttachmentInfo depthAttachInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = depthImageView,
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
			.extent{.width = swapchainWidth, .height = swapchainHeight}
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachInfo,
		.pDepthAttachment = &depthAttachInfo
	};

	// begin dynamic rendering
	vkCmdBeginRendering(res.commandBuffer, &renderingInfo);
	{
		// set the viewpot and scissor state
		VkViewport viewport
		{
			.x = 0, .y = 0,
			.width = static_cast<float>(swapchainWidth),
			.height = static_cast<float>(swapchainHeight)
		};
		vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);

		VkRect2D scissor
		{
			.offset{.x = 0, .y = 0 },
			.extent{.width = swapchainWidth, .height = swapchainHeight}
		};
		vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

		// draw our triangle
		vkCmdBindPipeline(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		for (const Node &node : nodeWorld.allNodes())
		{
			if (node.meshId)
			{
				// look up the mesh and associated buffer
				Mesh &mesh = meshes[node.meshId - 1];
				GPUBuffer &vertBuffer = buffers[mesh.vertexBufferId - 1];
				GPUBuffer &idxBuffer = buffers[mesh.indexBufferId - 1];
				DrawConstants drawConsts
				{
					.vertexBufferAddress = vertBuffer.deviceAddress
				};
				vkCmdPushConstants(res.commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawConstants), &drawConsts);

				vkCmdBindIndexBuffer(res.commandBuffer, idxBuffer.vkBuffer, 0, VK_INDEX_TYPE_UINT32);

				for (SubMesh &subMesh : mesh.subMeshes)
				{
					vkCmdDrawIndexed(res.commandBuffer, subMesh.indexCount, 1, subMesh.indexStart, subMesh.vertexStart, 0);
				}
			}
		}
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
		.image = swapchainImages[imageIndex],
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
	std::vector<VkSemaphoreSubmitInfo> semaphoreSignals
	{
		{ // render work completion signal
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = renderCompleteSemaphores[imageIndex],
			.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
		},
		{ // entire frame is completed (timeline)
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = timelineSemaphore,
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
	vkQueueSubmit2(gfxQueue, 1, &submitInfo, VK_NULL_HANDLE);

	// present the image
	VkPresentInfoKHR presentInfo{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &renderCompleteSemaphores[imageIndex], // render work completed semaphore
		.swapchainCount = 1,
		.pSwapchains = &swapchain,
		.pImageIndices = &imageIndex,
		.pResults = nullptr
	};

	vkQueuePresentKHR(gfxQueue, &presentInfo);
}

//VkCommandBuffer Application::startTransientCommandBuffer()
//{
//	// allocate the transient command buffer
//	VkCommandBufferAllocateInfo cmdAllocInfo
//	{
//		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
//		.commandPool = commandPool,
//		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
//		.commandBufferCount = 1,
//	};
//
//	VkCommandBuffer commandBuffer = nullptr;
//	if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer) != VK_SUCCESS)
//	{
//		showError("Unable to allocate command buffer");
//		return nullptr;
//	}
//
//	// begin the command buffer
//	VkCommandBufferBeginInfo beginInfo
//	{
//		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
//		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
//	};
//	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
//	{
//		showError("Unable to begin command buffer");
//		return nullptr;
//	}
//	return commandBuffer;
//}
//
//void Application::submitTransientCommandBuffer(VkCommandBuffer commandBuffer)
//{
//	vkEndCommandBuffer(commandBuffer);
//
//	VkSubmitInfo submitInfo
//	{
//		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
//		.commandBufferCount = 1,
//		.pCommandBuffers = &commandBuffer
//	};
//
//	vkQueueSubmit(gfxQueue, 1, &submitInfo, nullptr);
//	vkQueueWaitIdle(gfxQueue);
//	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
//}

std::pair<uint32_t, GPUBuffer> Application::createTexture(VkCommandBuffer commandBuffer, unsigned char *imageData, uint32_t width, uint32_t height, int channels)
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
	VmaAllocationCreateInfo allocInfo{ .usage = VMA_MEMORY_USAGE_CPU_TO_GPU };
	GPUTexture texture;
	if (vmaCreateImage(vmaAllocator, &imageInfo, &allocInfo, &texture.image, &texture.allocation, nullptr) != VK_SUCCESS)
	{
		showError("Error creating image");
		return { 0, GPUBuffer{} };
	}

	VkImageViewCreateInfo imgViewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = texture.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = imageFormat,
		.subresourceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1
		}
	};
	if (vkCreateImageView(device, &imgViewInfo, nullptr, &texture.imageView) != VK_SUCCESS)
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
		.image = texture.image,
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
	GPUBuffer stageBuff = createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, byteSize, imageData);

	VkBufferImageCopy buffImgCopy
	{
		.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
		.imageExtent = {.width = width, .height = height, .depth = 1 },
	};
	vkCmdCopyBufferToImage(commandBuffer, stageBuff.vkBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffImgCopy);

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
		.image = texture.image,
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

	textures.push_back(texture);
	const uint32_t textureId = textures.size();
	return { textureId, stageBuff };
	/*
	// ensure the image is in shader-read-optimal layout
	VkHostImageLayoutTransitionInfo transition
	{
		.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO,
		.image = texture.image,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
	};
	if (!vkTransitionImageLayout(device, 1, &transition))
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
	if (vkCopyMemoryToImage(device, &copyInfo) != VK_SUCCESS)
	{
		showError("Error copying image data");
		return 0;
	}
	*/
}

bool Application::createDescriptorSets()
{
	// create a pool to accomodate all descriptor sets
	std::array<VkDescriptorPoolSize, 2> poolSizes{
		VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = MaxTextures},
		VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = MaxFramesInFlight} };
	VkDescriptorPoolCreateInfo poolInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
										.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
										.maxSets = MaxFramesInFlight + 1,
										.poolSizeCount = poolSizes.size(),
										.pPoolSizes = poolSizes.data() };
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descPool) != VK_SUCCESS)
	{
		showError("Unable to create descriptor pool");
		return false;
	}

	// global descriptor set
	{
		std::array<VkDescriptorSetLayoutBinding, 1> bindings = {
			VkDescriptorSetLayoutBinding{.binding = 0,
										 .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
										 .descriptorCount = MaxTextures,
										 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT} };
		std::array<VkDescriptorBindingFlags, 1> flags;
		flags[0] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
			.bindingCount = flags.size(),
			.pBindingFlags = flags.data() };

		VkDescriptorSetLayoutCreateInfo layoutInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
												   .pNext = &flagsInfo,
												   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
												   .bindingCount = bindings.size(),
												   .pBindings = bindings.data() };

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &globalDSLayout) != VK_SUCCESS)
		{
			showError("Unable to create descriptor set layout");
			return false;
		}

		// create the actual descriptor sets
		VkDescriptorSetAllocateInfo descSetAllocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &globalDSLayout,
		};
		if (vkAllocateDescriptorSets(device, &descSetAllocInfo, &globalDescSet) != VK_SUCCESS)
		{
			showError("Unable to allocate descriptor set");
			return false;
		}
	}

	// frame descriptor set
	{
		std::array<VkDescriptorSetLayoutBinding, 1> bindings = {
			VkDescriptorSetLayoutBinding{.binding = 0,
										 .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
										 .descriptorCount = 1,
										 .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT} };

		std::array<VkDescriptorBindingFlags, 1> flags;
		flags[0] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
			.bindingCount = flags.size(),
			.pBindingFlags = flags.data() };

		VkDescriptorSetLayoutCreateInfo layoutInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
												   .pNext = &flagsInfo,
												   .flags = 0,
												   .bindingCount = bindings.size(),
												   .pBindings = bindings.data() };

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &frameDSLayout) != VK_SUCCESS)
		{
			showError("Unable to create descriptor set layout");
			return false;
		}

		// per-frame descriptor set creation
		VkDescriptorSetAllocateInfo descSetAllocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &frameDSLayout,
		};

		for (auto &res : frameResources)
		{
			if (vkAllocateDescriptorSets(device, &descSetAllocInfo, &res.descSet) != VK_SUCCESS)
			{
				showError("Unable to allocate descriptor set");
				return false;
			}
		}
	}

	return true;
}

void Application::updateGPUTextures()
{
}

GPUBuffer Application::createBuffer(VkBufferUsageFlags usage, size_t byteSize, void *initData)
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
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO
	};
	GPUBuffer gpuBuff;
	if (vmaCreateBuffer(vmaAllocator, &buffInfo, &allocInfo, &gpuBuff.vkBuffer, &gpuBuff.allocation, nullptr) != VK_SUCCESS)
	{
		return GPUBuffer{};
	}

	// map and write buffer data
	void *buffPtr = nullptr;
	if (vmaMapMemory(vmaAllocator, gpuBuff.allocation, &buffPtr) != VK_SUCCESS)
	{
		vmaDestroyBuffer(vmaAllocator, gpuBuff.vkBuffer, gpuBuff.allocation);
		return GPUBuffer{};
	}
	std::memcpy(static_cast<char *>(buffPtr), initData, buffInfo.size);
	vmaUnmapMemory(vmaAllocator, gpuBuff.allocation);

	// BDA Send Device Pointer
	VkBufferDeviceAddressInfo vertBdaInfo
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = gpuBuff.vkBuffer
	};
	gpuBuff.deviceAddress = vkGetBufferDeviceAddress(device, &vertBdaInfo);

	return gpuBuff;
}

uint32_t Application::addBuffer(const GPUBuffer &buffer)
{
	buffers.push_back(buffer);
	uint32_t bufferId = buffers.size();
	return bufferId;
}

uint32_t Application::createMaterial(GPUMaterial &&gpuMat)
{
	materials.push_back(std::move(gpuMat));
	return materials.size();
}

uint32_t Application::addMesh(Mesh &&mesh)
{
	meshes.push_back(std::move(mesh));
	return meshes.size();
}

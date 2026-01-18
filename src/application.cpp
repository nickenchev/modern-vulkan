#include "application.h"
#include "utils.h"

#include <SDL3/SDL.h>
#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <iostream>
#include <tiny_gltf.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>


void Application::showError(const std::string &errorMessage) const
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessage.c_str(), window);
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

	loadModel();

	return true;
}

void Application::shutdown()
{
	// wait in case resources are in use
	vkDeviceWaitIdle(device);

	// clean up images
	for (const Renderer::Image &image : images)
	{
		vmaDestroyImage(vmaAllocator, image.handle, image.allocation);
		vkDestroyImageView(device, image.view, nullptr);
	}
	images.clear();

	// single-use command buffer pool
	vkDestroyCommandPool(device, commandPool, nullptr);

	// destroy allocated buffers
	vmaDestroyBuffer(vmaAllocator, vertexBuffer.buffer, vertexBuffer.allocation);
	vmaDestroyBuffer(vmaAllocator, indexBuffer.buffer, indexBuffer.allocation);

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

	// pipeline cleanup
	if (pipeline.layout)
	{
		vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
	}
	if (pipeline.handle)
	{
		vkDestroyPipeline(device, pipeline.handle, nullptr);
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
	prevTime = SDL_GetTicks();
	while (running)
	{
		nowTime = SDL_GetTicks();
		float deltaTime = (nowTime - prevTime) / 1000.0f;
		prevTime = nowTime;
		globalTime += deltaTime;

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
		render(deltaTime);
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

	if (pipeline = createGraphicsPipeline(); !pipeline.handle)
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

	// default to the first GPU
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
	VkPhysicalDeviceVulkan14Features supportedFeatures14{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext = nullptr };
	VkPhysicalDeviceVulkan13Features supportedFeatures13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &supportedFeatures14 };
	VkPhysicalDeviceVulkan12Features supportedFeatures12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supportedFeatures13 };
	VkPhysicalDeviceFeatures2 supportedFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supportedFeatures12 };
	vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);

	// check if what we need is supported
	if (!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2 ||
		!supportedFeatures12.timelineSemaphore)
	{
		showError("Physical device doesn't meet the feature requirements");
		return false;
	}

	// produce a separate features struct chain for device creation
	VkPhysicalDeviceVulkan14Features features14
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = nullptr
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
		.scalarBlockLayout = VK_TRUE,
		.timelineSemaphore = VK_TRUE,
		.bufferDeviceAddress = VK_TRUE
	};
	VkPhysicalDeviceFeatures2 features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features12,
		.features {.shaderInt64 = VK_TRUE }
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
		.imageFormat = swapchainFormat,
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
			.format = swapchainFormat,
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
		.format = depthFormat,
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
		.format = depthFormat,
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

Pipeline Application::createGraphicsPipeline() const
{
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
		.cullMode = VK_CULL_MODE_NONE,
		//.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.lineWidth = 1.0f
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
		.pColorAttachmentFormats = &swapchainFormat,
		.depthAttachmentFormat = depthFormat,
	};

	// Create the graphics pipeline
	Pipeline pipeline;

	// need to define a pipeline layout
	VkPushConstantRange pushConstRange
	{
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(Renderer::DrawConstants)
	};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstRange
	};
	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipeline.layout) != VK_SUCCESS)
	{
		showError("Unable to create the pipeline layout");
		return Pipeline{};
	}

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
		.layout = pipeline.layout,
		.renderPass = VK_NULL_HANDLE,
	};
	if (vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &pipeline.handle) != VK_SUCCESS)
	{
		showError("Error creating the pipeline");
		return Pipeline{};
	}
	return pipeline;
}

bool Application::createSyncResources()
{
	VkSemaphoreTypeCreateInfo semaphoreTypeInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = timelineValue
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
	// create a command pool for single use
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

void Application::render(float deltaTime)
{
	// first check if our swapchain is still valid
	if (requireSwapchainRecreate)
	{
		vkDeviceWaitIdle(device);
		destroySwapchain();
		createSwapchain(width, height);
		requireSwapchainRecreate = false;
	}

	const uint32_t frameResIndex = frameCounter++ % MaxFramesInFlight;
	// wait for frame using this frame's resources to complete
	uint64_t frameId = ++timelineValue; // this is our frame "ID", and what we're using to signal the end of this frame later
	uint64_t waitForId = frameId - MaxFramesInFlight; // frame N and frame N - MaxInFlight share resources (3 - 2 = 1 -- frame 3 and 1 share resources)

	VkSemaphoreWaitInfo waitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &timelineSemaphore,
		.pValues = &waitForId
	};
	vkWaitSemaphores(device, &waitInfo, UINT64_MAX);

	// now its safe to start recording commands
	FrameResources &res = frameResources[frameResIndex];
	vkResetCommandPool(device, res.commandPool, 0); // resets all buffers

	// get the resources for this frame
	VkSemaphore imageAcquireSemaphore = frameResources[frameResIndex].imageAcquiredSemaphore;

	// acquire the swapchain image, no need to wait for timeline semaphore just to then wait for the swapchain image
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
			.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, // both specified to control memory access at both stages (write)
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
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
		.clearValue{.color{0.0f, 0.0f, 0.0f, 1}}
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
			.height = static_cast<float>(swapchainHeight),
			.minDepth = 0,
			.maxDepth = 1.0f,
		};
		vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);

		VkRect2D scissor
		{
			.offset{.x = 0, .y = 0 },
			.extent{.width = swapchainWidth, .height = swapchainHeight}
		};
		vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);

		constexpr float fov = glm::radians(45.0);
		float aspect = static_cast<float>(width) / static_cast<float>(height);
		float nearP = 0.1f;
		float farP = 32.0f;

		glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)width / (float)height, nearP, farP);
		proj[1][1] *= -1;
		glm::mat4 rotation = glm::rotate(glm::mat4(1), static_cast<float>(globalTime), glm::vec3(0, 1, 0));
		glm::mat4 translate = glm::translate(glm::mat4(1), glm::vec3(0, -0.4, -1));
		glm::mat4 scale = glm::scale(glm::mat4(1), glm::vec3(1.0f, 1.0f, 1.0f));
		glm::mat4 transform = translate * rotation * scale;

		// BDA Send Device Pointer
		VkBufferDeviceAddressInfo vertBdaInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = vertexBuffer.buffer };
		Renderer::DrawConstants pushConsts
		{
			.vertexBufferAddress = vkGetBufferDeviceAddress(device, &vertBdaInfo),
			.globalTime = static_cast<float>(globalTime),
			.mvp = proj * transform
		};
		vkCmdPushConstants(res.commandBuffer, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Renderer::DrawConstants), &pushConsts);

		vkCmdBindIndexBuffer(res.commandBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
		for (Renderer::Mesh &mesh : meshes)
		{
			for (Renderer::SubMesh &sub : mesh.subMeshes)
			{
				vkCmdDrawIndexed(res.commandBuffer, sub.indexCount, 1, sub.indexStart, sub.vertexStart, 0);
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
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | // wait before drawing to image
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT // prevent depth buffer clearing before image is ready
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
			.value = frameId, // we're signalling our current frame ID is complete
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

void loadNode(tinygltf::Node &node, tinygltf::Model &model)
{
	using namespace tinygltf;

	for (int childNodeIndex : node.children)
	{
		loadNode(model.nodes[childNodeIndex], model);
	}
}

void Application::loadModel()
{
	using namespace tinygltf;
	Model model;
	TinyGLTF loader;

	std::string err;
	std::string warn;
	//loader.LoadASCIIFromFile(&model, &err, &warn, "C:/Users/nikol/Desktop/untitled.gltf");
	loader.LoadASCIIFromFile(&model, &err, &warn, "D:/glTF-Sample-Models/2.0/FlightHelmet/glTF/FlightHelmet.gltf");
	//loader.LoadASCIIFromFile(&model, &err, &warn, "S:/projects/boiler-3d/data/sorceress/scene.gltf");

	// load images
	VkCommandBuffer commandBuffer = startTransientCommandBuffer();
	if (commandBuffer)
	{
		for (const Image &image : model.images)
		{
			Renderer::Image newImage = createImage(image.image, image.width, image.height, image.component);
			if (newImage.handle == nullptr)
			{
				break;
			}
			images.push_back(newImage);
		}
	}
	submitTransientCommandBuffer(commandBuffer);

	// load all meshes first
	for (const Mesh &mesh : model.meshes)
	{
		Renderer::Mesh newMesh;
		for (const Primitive &primitive : mesh.primitives)
		{
			Renderer::SubMesh subMesh;
			subMesh.vertexStart = vertices.size();
			subMesh.indexStart = indices.size();

			// load primitive vertices into sub-mesh
			if (const auto &itr = primitive.attributes.find("POSITION"); itr != primitive.attributes.end())
			{
				const auto &[name, index] = *itr;
				const Accessor &access = model.accessors[index];
				const BufferView &bv = model.bufferViews[access.bufferView];
				const tinygltf::Buffer &buffer = model.buffers[bv.buffer];

				if (access.type == TINYGLTF_TYPE_VEC3)
				{
					for (int i = 0; i < access.count; ++i)
					{
						size_t offset = bv.byteOffset + access.byteOffset + i * ((bv.byteStride > 0) ? bv.byteStride : sizeof(glm::vec3));
						const glm::vec3 *pos = reinterpret_cast<const glm::vec3 *>(buffer.data.data() + offset);
						vertices.push_back(Renderer::Vertex{ .position = *pos });
						subMesh.vertexCount++;
					}
				}
			}
			// load primitive vertices into sub-mesh
			if (const auto &itr = primitive.attributes.find("TEXCOORD_0"); itr != primitive.attributes.end())
			{
				const auto &[name, index] = *itr;
				const Accessor &access = model.accessors[index];
				const BufferView &bv = model.bufferViews[access.bufferView];
				const tinygltf::Buffer &buffer = model.buffers[bv.buffer];

				if (access.type == TINYGLTF_TYPE_VEC2)
				{
					for (int i = 0; i < access.count; ++i)
					{
						size_t offset = bv.byteOffset + access.byteOffset + i * ((bv.byteStride > 0) ? bv.byteStride : sizeof(glm::vec2));
						const glm::vec2 *uv = reinterpret_cast<const glm::vec2 *>(buffer.data.data() + offset);
						vertices[subMesh.vertexStart + i].uv = *uv;
					}
				}
			}
			// indices
			if (primitive.indices != -1)
			{
				const Accessor &access = model.accessors[primitive.indices];
				const BufferView &bv = model.bufferViews[access.bufferView];
				const tinygltf::Buffer &buffer = model.buffers[bv.buffer];
				subMesh.indexCount = access.count;

				if (access.type == TINYGLTF_TYPE_SCALAR && access.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
				{
					for (int i = 0; i < access.count; ++i)
					{
						const uint32_t *idx = reinterpret_cast<const uint32_t *>(buffer.data.data() + bv.byteOffset + access.byteOffset) + i;
						indices.push_back(*idx);
					}
				}
				else if (access.type == TINYGLTF_TYPE_SCALAR && access.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
				{
					for (int i = 0; i < access.count; ++i)
					{
						const uint16_t *idx = reinterpret_cast<const uint16_t *>(buffer.data.data() + bv.byteOffset + access.byteOffset) + i;
						indices.push_back(*idx);
					}
				}
			}
			newMesh.subMeshes.push_back(subMesh);
		}
		meshes.push_back(newMesh);
	}

	auto createBuffer = [](VmaAllocator &vmaAllocator, VkBufferUsageFlags usage, size_t byteSize, void *initData)
	{
		VkBufferCreateInfo buffInfo
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = byteSize,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE
		};

		VmaAllocationCreateInfo allocInfo
		{
			.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
			.usage = VMA_MEMORY_USAGE_CPU_TO_GPU
		};

		Renderer::Buffer newBuff;
		if (vmaCreateBuffer(vmaAllocator, &buffInfo, &allocInfo, &newBuff.buffer, &newBuff.allocation, nullptr) != VK_SUCCESS)
		{
			//showError("Error allocating buffer");
		}

		void *buffPtr = nullptr;
		if (vmaMapMemory(vmaAllocator, newBuff.allocation, &buffPtr) != VK_SUCCESS)
		{
			//showError("Unable to map buffer memory");
		}
		std::memcpy(static_cast<char *>(buffPtr), initData, buffInfo.size);
		const glm::vec3 *vec3Ptr = reinterpret_cast<const glm::vec3 *>(buffPtr);
		vmaUnmapMemory(vmaAllocator, newBuff.allocation);

		return newBuff;
	};

	vertexBuffer = createBuffer(vmaAllocator, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		sizeof(Renderer::Vertex) * vertices.size(), vertices.data());
	indexBuffer = createBuffer(vmaAllocator, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		sizeof(uint32_t) * indices.size(), indices.data());
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

Renderer::Image Application::createImage(std::vector<unsigned char> imageData, uint32_t width, uint32_t height, int components)
{
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

	Renderer::Image image;
	if (vmaCreateImage(vmaAllocator, &imageInfo, &allocInfo, &image.handle, &image.allocation, nullptr) != VK_SUCCESS)
	{
		showError("Error creating image");
		return Renderer::Image{};
	}

	VkImageViewCreateInfo imgViewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image.handle,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = imageFormat,
		.subresourceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1
		}
	};

	if (vkCreateImageView(device, &imgViewInfo, nullptr, &image.view) != VK_SUCCESS)
	{
		showError("Error creating image view");
		return Renderer::Image{};
	}

	return image;
}

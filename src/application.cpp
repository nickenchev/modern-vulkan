#include "application.h"

#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <shaderc/shaderc.hpp>
#include <iostream>

void Application::showError(const std::string &errorMessasge) const
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessasge.c_str(), window);
}

std::vector<uint32_t> compileShader(const std::string source, shaderc_shader_kind kind, const std::string &fileName)
{
	shaderc::Compiler compiler;
	shaderc::CompileOptions opts;

	opts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
	opts.SetTargetSpirv(shaderc_spirv_version_1_6);
	opts.SetOptimizationLevel(shaderc_optimization_level_performance);

	shaderc::CompilationResult result = compiler.CompileGlslToSpv(source, kind, fileName.c_str(), opts);
	if (result.GetCompilationStatus() != shaderc_compilation_status_success)
	{
		std::cerr << "Shader Compilation Error: " << result.GetErrorMessage() << std::endl;
	}
	return { result.cbegin(), result.cend() };
}

bool Application::initialize()
{
	SDL_InitSubSystem(SDL_INIT_VIDEO);
	window = SDL_CreateWindow("Vulkan Learning", width, height, SDL_WINDOW_VULKAN);
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

void Application::shutdown()
{
	// cleanup allocated resources
	vkDestroyImageView(device, depthImageView, nullptr);
	vmaDestroyImage(vmaAllocator, depthImage, depthImageAllocation);

	// cleanup swapchain
	for (VkImageView swapchainImgView : swapchainImageViews)
	{
		vkDestroyImageView(device, swapchainImgView, nullptr);
	}
	swapchainImageViews.clear();

	if (swapchain)
	{
		vkDestroySwapchainKHR(device, swapchain, nullptr);
	}

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

void Application::start()
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
			}
		}
	}
}

bool Application::initializeVulkan()
{
	if (vulkanInstance = createVulkanInstance(); !vulkanInstance)
	{
		showError("Couldn't create a vulkan instance");
		return false;
	}

	if (surface = createSurface(); !surface)
	{
		showError("Couldn't create window surface");
		return false;
	}

	if (physicalDevice = findPhysicalDevice(); !physicalDevice)
	{
		showError("Unable to find an appropriate physical device");
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

	if (swapchain = createSwapchain(width, height); !swapchain)
	{
		showError("Unable to create swapchain");
		return false;
	}

	return true;
}

VkInstance Application::createVulkanInstance() const
{
	// Initialize Volk and load Vk function pointers
	if (volkInitialize() != VK_SUCCESS)
	{
		showError("Error initializing Volk");
		return nullptr;
	}

	// Create the vulkan application instance
	VkApplicationInfo appInfo
	{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Vulkan Learning",
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

	VkInstance instance = nullptr;
	if (vkCreateInstance(&instCreateInfo, nullptr, &instance) != VK_SUCCESS)
	{
		return nullptr;
	}

	volkLoadInstance(instance);
	return instance;
}

VkSurfaceKHR Application::createSurface() const
{
	VkSurfaceKHR surface = nullptr;
	SDL_Vulkan_CreateSurface(window, vulkanInstance, nullptr, &surface);
	return surface;
}

VkPhysicalDevice Application::findPhysicalDevice()
{
	// enumerate all physical devices
	uint32_t physDeviceCount = 0;
	vkEnumeratePhysicalDevices(vulkanInstance, &physDeviceCount, nullptr);
	std::vector<VkPhysicalDevice> physicalDevices(physDeviceCount);
	vkEnumeratePhysicalDevices(vulkanInstance, &physDeviceCount, physicalDevices.data());

	// find an appropriate physical device (GPU)
	for (auto &pd : physicalDevices)
	{
		bool hasGfxQueue = false;
		bool hasPresentQueue = false;

		// device has a graphics queue, ensure it has swapchain support
		uint32_t extCount = 0;
		vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(extCount);
		vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, availableExtensions.data());

		bool hasSwapchainSupport = false;
		for (const auto &ext : availableExtensions)
		{
			if (strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
			{
				hasSwapchainSupport = true;
				break;
			}
		}

		if (hasSwapchainSupport)
		{
			// grab all of the queue families
			uint32_t queueFamCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties2(pd, &queueFamCount, nullptr);
			std::vector<VkQueueFamilyProperties2> queueFamProps(queueFamCount, { VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
			vkGetPhysicalDeviceQueueFamilyProperties2(pd, &queueFamCount, queueFamProps.data());

			for (int currentFamIdx = 0; currentFamIdx < queueFamProps.size(); currentFamIdx++)
			{
				const auto &props = queueFamProps[currentFamIdx];
				if (props.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					gfxQueueFamIdx = currentFamIdx;
					hasGfxQueue = true;
				}

				// ensure it has presentation support
				VkBool32 hasPresentSupport = false;
				if (vkGetPhysicalDeviceSurfaceSupportKHR(pd, currentFamIdx, surface, &hasPresentSupport) == VK_SUCCESS)
				{
					if (hasPresentSupport)
					{
						presentQueueFamIdx = currentFamIdx;
						hasPresentQueue = true;
					}
				}
				// prefer queue familes that have graphics and presenet
				if (hasGfxQueue && hasPresentQueue)
				{
					return pd;
				}
			}
		}
	}
	return nullptr;
}

bool Application::createDevice(VkPhysicalDevice physicalDevice)
{
	float queuePriority = 1.0f;
	std::vector<uint32_t> queueFamiles{ gfxQueueFamIdx };

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{
		VkDeviceQueueCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = gfxQueueFamIdx,
			.queueCount = 1,
			.pQueuePriorities = &queuePriority
		}
	};
	if (gfxQueueFamIdx != presentQueueFamIdx)
	{
		queueCreateInfos.push_back(
			VkDeviceQueueCreateInfo
			{
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.queueFamilyIndex = presentQueueFamIdx,
				.queueCount = 1,
				.pQueuePriorities = &queuePriority
			});
	}

	// setup the vulka feature chain for querying
	VkPhysicalDeviceVulkan14Features features14
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = nullptr
	};
	VkPhysicalDeviceVulkan13Features features13
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &features14
	};
	VkPhysicalDeviceVulkan12Features features12
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &features13
	};
	VkPhysicalDeviceFeatures2 features
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features12
	};
	// query all features / will enable all
	vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

	const std::vector<const char *> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo devCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features,
		.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
		.pQueueCreateInfos = queueCreateInfos.data(),
		.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
		.ppEnabledExtensionNames = deviceExtensions.data(),
		.pEnabledFeatures = nullptr // features struct chain is set in pNext
	};

	if (vkCreateDevice(physicalDevice, &devCreateInfo, nullptr, &device) != VK_SUCCESS)
	{
		return false;
	}

	// get the needed device queues (gfx only for now)
	vkGetDeviceQueue(device, gfxQueueFamIdx, 0, &gfxQueue);
	if (!gfxQueue)
	{
		showError("Couldn't get the graphics queue");
		return false;
	}
	presentQueue = gfxQueue; // initially assume both queues are the same

	// if the graphics and present queue familes are different
	// get another VkQueue for presentation
	if (gfxQueueFamIdx != presentQueueFamIdx)
	{
		vkGetDeviceQueue(device, presentQueueFamIdx, 0, &presentQueue);
		if (!presentQueue)
		{
			showError("Couldn't get the present queue");
			return false;
		}
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

VkSwapchainKHR Application::createSwapchain(uint32_t width, uint32_t height)
{
	VkSurfaceCapabilitiesKHR surfaceCaps{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps) != VK_SUCCESS)
	{
		showError("Couldn't get the surface capabilities");
		return nullptr;
	}

	const VkFormat imageFormat{ VK_FORMAT_B8G8R8A8_SRGB };
	VkSwapchainCreateInfoKHR swapchainCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = surfaceCaps.minImageCount,
		.imageFormat = imageFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent{.width = width, .height = height },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR
	};

	VkSwapchainKHR swapchain = nullptr;
	if (vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain) != VK_SUCCESS)
	{
		showError("Error creating swapchain");
		return nullptr;
	}

	// grab the swapchain images
	uint32_t imageCount = 0;
	vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
	swapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
	swapchainImageViews.resize(imageCount);

	// create the swapchain image views
	for (int i = 0; i < imageCount; ++i)
	{
		VkImageViewCreateInfo imgViewInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = imageFormat,
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
			return nullptr;
		}
	}


	// create depth image
	const VkFormat depthFormat{ VK_FORMAT_D32_SFLOAT_S8_UINT };
	VkImageCreateInfo depthCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = depthFormat,
		.extent{.width = width, .height = height, .depth = 1 },
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
	VkImageCreateInfo imageCreateInfo{};
	if (vmaCreateImage(vmaAllocator, &depthCreateInfo, &allocInfo, &depthImage, &depthImageAllocation, nullptr) != VK_SUCCESS)
	{
		showError("Error allocating depth image");
		return nullptr;
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
		return nullptr;
	}

	return swapchain;
}


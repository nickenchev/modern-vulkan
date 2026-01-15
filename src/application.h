#pragma once

#define VK_NO_PROTOTYPES
#include <SDL3/SDL_vulkan.h>
#include <string>
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <string>
#include <shaderc/shaderc.hpp>

struct SDL_Window;
struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

struct Pipeline
{
	VkPipelineLayout layout = nullptr;
	VkPipeline handle = nullptr;
};

struct FrameResources
{
	VkCommandPool commandPool = nullptr;
	VkCommandBuffer commandBuffer = nullptr;
	VkSemaphore workCompleteSemaphore = nullptr;
};

class Application
{
	constexpr static uint32_t VulkanVersion{ VK_API_VERSION_1_4 };
	constexpr static uint32_t MaxFramesInFlight{ 2 };
	constexpr static VkFormat swapchainFormat{ VK_FORMAT_B8G8R8A8_SRGB };
	constexpr static VkFormat depthFormat{ VK_FORMAT_D32_SFLOAT };

	SDL_Window* window = nullptr;
	uint32_t width = 1280;
	uint32_t height = 720;
	bool running = false;
	uint64_t timelineValue = MaxFramesInFlight - 1; // subtract 1 to ensure wait-for-ID / frame resource index start at 0 during render, avoids if (frameId < MaxFramesInFlight) check

	// vulkan core
	VkInstance vulkanInstance = nullptr;
	VkPhysicalDevice physicalDevice = nullptr;
	VkDevice device = nullptr;
	VkSurfaceKHR surface = nullptr;
	VmaAllocator vmaAllocator = nullptr;

	// queue related
	uint32_t gfxQueueFamIdx = UINT32_MAX;
	VkQueue gfxQueue = nullptr;

	// swapchain related
	VkSwapchainKHR swapchain = nullptr;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	std::vector<VkSemaphore> imageAcquireSemaphores;
	bool requireSwapchainRecreate = false;
	uint32_t swapchainWidth = 0;
	uint32_t swapchainHeight = 0;

	VkImage depthImage = nullptr;
	VkImageView depthImageView = nullptr;
	VmaAllocation depthImageAllocation = nullptr;

	// graphics pipeline related
	Pipeline pipeline;

	// shader resources
	VkShaderModule vertShader = nullptr;
	VkShaderModule fragShader = nullptr;

	// frame and synchronization resources
	VkSemaphore timelineSemaphore = nullptr;
	std::array<FrameResources, MaxFramesInFlight> frameResources;

	void showError(const std::string &errorMessasge) const;

	bool initializeVulkan();
	bool createVulkanInstance();
	bool createSurface();
	VkPhysicalDevice findPhysicalDevice();
	bool findGraphicsQueue();
	bool createDevice(VkPhysicalDevice physicalDevice);
	bool initializeVMA();
	bool createSwapchain(uint32_t width, uint32_t height);
	void destroySwapchain();
	VkShaderModule createShaderModule(const std::string &fileName, shaderc_shader_kind kind) const;
	bool createShaders();
	Pipeline createGraphicsPipeline() const;
	bool createSyncResources();
	bool createCommandBuffers();
	void render();

public:
	bool initialize();
	void shutdown();
	void run();
};
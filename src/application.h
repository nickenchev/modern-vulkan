#pragma once

#include <SDL3/SDL.h>
#define VK_NO_PROTOTYPES
#include <SDL3/SDL_vulkan.h>
#include <string>
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <string>

struct SDL_Window;
struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

class Application
{
	constexpr static uint32_t VulkanVersion{ VK_API_VERSION_1_4 };
	constexpr static uint32_t MaxFramesInFlight{ 2 };

	SDL_Window* window = nullptr;
	int width = 1280;
	int height = 720;
	bool running = false;

	// vulkan core
	VkInstance vulkanInstance = nullptr;
	VkPhysicalDevice physicalDevice = nullptr;
	VkDevice device = nullptr;
	VkSurfaceKHR surface = nullptr;
	VmaAllocator vmaAllocator = nullptr;

	// queue related
	uint32_t presentQueueFamIdx = UINT32_MAX;
	uint32_t gfxQueueFamIdx = UINT32_MAX;
	VkQueue gfxQueue = nullptr;
	VkQueue presentQueue = nullptr;

	// swapchain related
	VkSwapchainKHR swapchain = nullptr;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;

	VkImage depthImage = nullptr;
	VkImageView depthImageView = nullptr;
	VmaAllocation depthImageAllocation = nullptr;

	//per frame resources


public:
	bool initialize();
	void shutdown();
	void start();

private:
	void showError(const std::string& errorMessasge) const;
	bool initializeVulkan();

	VkInstance createVulkanInstance() const;
	VkSurfaceKHR createSurface() const;
	VkPhysicalDevice findPhysicalDevice();
	bool createDevice(VkPhysicalDevice physicalDevice);
	bool initializeVMA();
	VkSwapchainKHR createSwapchain(uint32_t width, uint32_t height);

	std::string readTextFile(const std::string &filePath) const;
};
#pragma once

#define VK_NO_PROTOTYPES
#include <SDL3/SDL_vulkan.h>
#include <string>
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <shaderc/shaderc.hpp>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "resources.h"
#include "nodeworld.h"

struct SDL_Window;
struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

struct DrawConstants
{
	glm::mat4 wvp;
	glm::mat4 worldMatrix;
	uint64_t vertexBufferAddress = 0;
	uint64_t materialBufferAddress = 0;
	uint32_t materialIndex;
};

struct FrameResources
{
	VkCommandPool commandPool = nullptr;
	VkCommandBuffer commandBuffer = nullptr;
	VkSemaphore imageAcquiredSemaphore = nullptr;
	VkDescriptorSet descSet = nullptr;
};

struct GPUTexture
{
	VkImage image = nullptr;
	VkImageView imageView = nullptr;
	VmaAllocation allocation = nullptr;
};

struct GPUBuffer
{
	VkBuffer vkBuffer = nullptr;
	uint64_t deviceAddress = 0;
	VmaAllocation allocation = nullptr;
};

struct GPUMaterial
{
	glm::vec4 baseColor;
	uint32_t textureId;
};

class Application
{
	constexpr static uint32_t VulkanVersion{ VK_API_VERSION_1_4 };
	constexpr static uint32_t MaxFramesInFlight{ 2 };
	constexpr static size_t MaxTextures = 128;
	constexpr static VkFormat SwapchainFormat{ VK_FORMAT_B8G8R8A8_SRGB };
	constexpr static VkFormat DepthFormat{ VK_FORMAT_D32_SFLOAT };

	SDL_Window* m_window = nullptr;
	uint32_t m_width = 1280;
	uint32_t m_height = 720;
	bool m_running = false;
    uint64_t m_frameIndex = 0;
    uint64_t m_nextSignalValue = MaxFramesInFlight + 1;

	// vulkan core
	VkInstance m_vulkanInstance = nullptr;
	VkPhysicalDevice m_physicalDevice = nullptr;
	VkDevice m_device = nullptr;
	VkSurfaceKHR m_surface = nullptr;
	VmaAllocator m_vmaAllocator = nullptr;

	// queue related
	uint32_t m_gfxQueueFamIdx = UINT32_MAX;
	VkQueue m_gfxQueue = nullptr;
	VkCommandPool m_commandPool = nullptr;

	// swapchain related
	VkSwapchainKHR m_swapchain = nullptr;
	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;
	std::vector<VkSemaphore> m_renderCompleteSemaphores;
	bool m_requireSwapchainRecreate = false;
	uint32_t m_swapchainWidth = 0;
	uint32_t m_swapchainHeight = 0;

	VkImage m_depthImage = nullptr;
	VkImageView m_depthImageView = nullptr;
	VmaAllocation m_depthImageAllocation = nullptr;

	// graphics pipeline related
	VkPipelineLayout m_pipelineLayout = nullptr;
	VkPipeline m_pipeline = nullptr;

	// shader resources
	VkShaderModule m_vertShader = nullptr;
	VkShaderModule m_fragShader = nullptr;

	// frame and synchronization resources
	VkSemaphore m_timelineSemaphore = nullptr;
	std::array<FrameResources, MaxFramesInFlight> m_frameResources;

	// cpu resources
	std::vector<Mesh> m_meshes;

	// gpu resources
	VkSampler m_sampler = nullptr;
	std::vector<GPUTexture> m_textures;
	std::vector<GPUBuffer> m_buffers;
	std::vector<GPUMaterial> m_materials;
	uint32_t m_materialBufferId = 0;

	// descriptors
	VkDescriptorSetLayout m_globalDSLayout = nullptr;
	VkDescriptorSet m_globalDescSet = nullptr;
	VkDescriptorPool m_descPool = nullptr;

	// game nodes and scene data
	NodeWorld m_nodeWorld;
	std::vector<uint32_t> m_rootNodes;

	// camera related
	float m_camDistance = 3;
	float m_camRotation = glm::radians(90.0f);
	float m_camElevation = 0;

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
	VkPipeline createGraphicsPipeline();
	bool createSyncResources();
	bool createCommandBuffers();
	bool createDescriptorSets();
	void render();
	VkCommandBuffer startTransientCommandBuffer();
	void submitTransientCommandBuffer(VkCommandBuffer commandBuffer);
	std::pair<uint32_t, GPUBuffer> createTexture(VkCommandBuffer commandBuffer, unsigned char *imageData, uint32_t width, uint32_t height, int channels);
	GPUBuffer createBuffer(VkBufferUsageFlags usage, size_t byteSize, void *initData);
	uint32_t addBuffer(const GPUBuffer &buffer);
	uint32_t createMaterial(GPUMaterial &&gpuMat);
	uint32_t addMesh(Mesh &&mesh);
	uint32_t createNode(Node &&node);

public:
	bool initialize();
	bool loadData();
	void shutdown();
	void run();
};

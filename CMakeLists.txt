cmake_minimum_required(VERSION 3.24)
project(VulkanLearning)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(vulkanapp
	"src/main.cpp"
	"src/application.h"
	"src/application.cpp")
target_link_libraries(vulkanapp PRIVATE Vulkan::Vulkan SDL3::SDL3)


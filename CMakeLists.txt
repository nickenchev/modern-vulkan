cmake_minimum_required(VERSION 3.24)
project(VulkanLearning)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(glm REQUIRED)

add_executable(vulkanapp
	"src/main.cpp"
	"src/application.h"
	"src/application.cpp"
	"src/shaderdata.h"
)

set_property(TARGET vulkanapp PROPERTY CXX_STANDARD 23)

target_link_libraries(vulkanapp PRIVATE Vulkan::Vulkan SDL3::SDL3 glm::glm)


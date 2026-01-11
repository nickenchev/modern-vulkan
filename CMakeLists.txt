cmake_minimum_required(VERSION 3.24)
project(VulkanLearning)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS volk shaderc_combined)
find_package(glm REQUIRED)

add_executable(vulkanapp
	"src/main.cpp"
	"src/application.h"
	"src/application.cpp"
	"src/shaderdata.h"
	"src/shaders/shaders.h")

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For MSVC, you often need the 'latest' flag specifically
if(MSVC)
    target_compile_options(vulkanapp PRIVATE /std:c++latest)
endif()

target_link_libraries(vulkanapp PRIVATE
	Vulkan::Vulkan
	Vulkan::volk
	Vulkan::shaderc_combined
	SDL3::SDL3
	glm::glm)


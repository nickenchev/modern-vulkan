cmake_minimum_required(VERSION 3.24)
project(VulkanLearning)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS volk shaderc_combined)
find_package(glm REQUIRED)

add_executable(vulkanapp
	"src/main.cpp"
	"src/application.h"
	"src/application.cpp"
	 "src/utils.h"
	 "src/utils.cpp"
	 "src/ext/tiny_gltf_v3.h"
	 "src/ext/tinygltf_json.h"
	 "src/ext/tinygltf.cpp"
	 "src/gltfloader.h"
	 "src/gltfloader.cpp" "src/resources.h" "src/nodeworld.h")

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

target_include_directories(vulkanapp PRIVATE "src/ext/")


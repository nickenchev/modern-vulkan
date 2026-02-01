cmake_minimum_required(VERSION 3.24)
project(VulkanLearning)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS volk shaderc_combined)
find_package(glm REQUIRED)

set(SOURCE_FILES
	"src/main.cpp"
	"src/application.h"
	"src/application.cpp"
	 "src/utils.h"
	 "src/utils.cpp"
	 "src/ext/tiny_gltf.cc"
)

add_executable(vulkanapp ${SOURCE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_FILES})

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For MSVC, you often need the 'latest' flag specifically
if(MSVC)
    target_compile_options(vulkanapp PRIVATE /std:c++latest)
endif()

target_include_directories(vulkanapp PRIVATE "${CMAKE_SOURCE_DIR}/src/ext")
target_link_libraries(vulkanapp PRIVATE
	Vulkan::Vulkan
	Vulkan::volk
	Vulkan::shaderc_combined
	SDL3::SDL3
	glm::glm)


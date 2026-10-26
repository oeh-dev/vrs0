
cmake_minimum_required(VERSION 3.24)
project(VulkanRaiiSample LANGUAGES C CXX)
 
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # clangd용
 
# Vulkan SDK (헤더, 로더, vulkan_raii.hpp)
find_package(Vulkan REQUIRED)
 
# GLFW: 창과 입력
include(FetchContent)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)
 
add_executable(vulkan_raii_sample src/main.cpp)
target_link_libraries(vulkan_raii_sample PRIVATE Vulkan::Vulkan glfw)

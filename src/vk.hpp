#pragma once

// 모든 소스는 Vulkan과 GLFW를 이 헤더를 통해 포함한다.
// 설정 매크로(VULKAN_HPP_NO_CONSTRUCTORS 등)는 CMakeLists.txt에서 일괄 정의한다.

#include <vulkan/vulkan_raii.hpp>

// Vulkan 헤더를 먼저 포함해야 GLFW가 glfwCreateWindowSurface를 선언한다.
#include <GLFW/glfw3.h>

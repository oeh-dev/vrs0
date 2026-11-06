#include "Window.hpp"

#include <stdexcept>

Window::Session::Session() {
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
}

Window::Session::~Session() { glfwTerminate(); }

Window::Window(int width, int height, const char* title) {
    if (!glfwVulkanSupported()) throw std::runtime_error("GLFW: Vulkan loader not found");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // OpenGL 컨텍스트를 만들지 않는다
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window_.reset(glfwCreateWindow(width, height, title, nullptr, nullptr));
    if (!window_) throw std::runtime_error("glfwCreateWindow failed");
}

bool Window::shouldClose() const { return glfwWindowShouldClose(window_.get()); }

void Window::requestClose() { glfwSetWindowShouldClose(window_.get(), GLFW_TRUE); }

bool Window::isKeyPressed(int key) const { return glfwGetKey(window_.get(), key) == GLFW_PRESS; }

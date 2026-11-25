#include "Window.hpp"

#include <stdexcept>
#include <utility>

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

    // 콜백은 C 함수 포인터라 객체를 직접 받을 수 없다. 창에 이 객체의 주소를 달아 두고 콜백에서 꺼낸다.
    glfwSetWindowUserPointer(window_.get(), this);
    glfwSetKeyCallback(window_.get(), &Window::keyCallback);
}

void Window::keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
    // GLFW_REPEAT(누르고 있을 때의 자동 반복)와 GLFW_RELEASE는 무시한다.
    if (action != GLFW_PRESS || key == GLFW_KEY_UNKNOWN) return;
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    self->keyPresses_.push_back(KeyPress{.key = key, .mods = mods});
}

bool Window::shouldClose() const { return glfwWindowShouldClose(window_.get()); }

void Window::requestClose() { glfwSetWindowShouldClose(window_.get(), GLFW_TRUE); }

void Window::setTitle(const std::string& title) { glfwSetWindowTitle(window_.get(), title.c_str()); }

void Window::pollEvents() { glfwPollEvents(); }

double Window::time() const { return glfwGetTime(); }

bool Window::isKeyDown(int key) const { return glfwGetKey(window_.get(), key) == GLFW_PRESS; }

std::vector<Window::KeyPress> Window::takeKeyPresses() { return std::exchange(keyPresses_, {}); }

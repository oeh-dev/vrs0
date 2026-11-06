#pragma once

#include <memory>

#include "vk.hpp"

// GLFW 초기화와 창 하나를 소유한다.
class Window {
public:
    Window(int width, int height, const char* title);

    GLFWwindow* handle() const { return window_.get(); }
    bool shouldClose() const;
    void requestClose();
    bool isKeyPressed(int key) const;

private:
    // glfwInit/glfwTerminate를 RAII로 묶는다. 창보다 먼저 선언해 창보다 나중에 소멸한다.
    struct Session {
        Session();
        ~Session();
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
    };
    struct Deleter {
        void operator()(GLFWwindow* w) const { glfwDestroyWindow(w); }
    };

    Session session_;
    std::unique_ptr<GLFWwindow, Deleter> window_;
};

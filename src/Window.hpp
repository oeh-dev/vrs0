#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vk.hpp"

// GLFW 초기화와 창 하나, 그리고 그 창의 입력을 소유한다.
class Window {
public:
    // 한 번 눌린 키. 누르고 있는 동안의 자동 반복은 포함하지 않는다.
    struct KeyPress {
        int key;   // GLFW_KEY_* (자판 배열과 상관없이 US 배열 기준의 물리적 위치)
        int mods;  // GLFW_MOD_SHIFT 등의 조합
    };

    Window(int width, int height, const char* title);

    // GLFW가 콜백에 이 객체의 주소를 넘겨주므로, 만든 뒤에 옮겨지면 안 된다.
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    GLFWwindow* handle() const { return window_.get(); }
    bool shouldClose() const;
    void requestClose();
    void setTitle(const std::string& title);

    // 쌓인 창 이벤트를 처리한다. 키 콜백은 이 안에서 불린다. 매 프레임 한 번 부른다.
    void pollEvents();

    // GLFW 초기화 이후 흐른 시간(초)
    double time() const;

    // 지금 눌려 있는가. 누르고 있는 동안 계속 움직이는 동작(카메라 등)에 쓴다.
    bool isKeyDown(int key) const;

    // 지난번 이후 새로 눌린 키들을 꺼낸다. 한 번 누르면 한 번 일어나는 동작(선택 전환 등)에 쓴다.
    std::vector<KeyPress> takeKeyPresses();

private:
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

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
    std::vector<KeyPress> keyPresses_;
};

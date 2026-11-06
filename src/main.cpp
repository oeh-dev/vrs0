// Vulkan RAII 샘플
//
// 수명 스코프에 따라 나눈 구성:
//   Window         GLFW 초기화와 창
//   VulkanContext  앱 수명: 인스턴스, 디바이스, 큐 등
//   Swapchain      스왑체인 수명: 화면 크기가 바뀌면 다시 만드는 것들
//   Renderer       프레임 수명: 매 프레임 기록하고 제출하는 것들

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "Renderer.hpp"
#include "Swapchain.hpp"
#include "VulkanContext.hpp"
#include "Window.hpp"

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

class App {
public:
    explicit App(std::optional<size_t> requestedDevice)
        : window_(kWidth, kHeight, "Vulkan RAII Sample"),
          context_(window_.handle(), requestedDevice),
          swapchain_(context_, window_.handle()),
          renderer_(context_, swapchain_) {}

    // 멤버가 소멸하기 전에, GPU가 아직 사용 중인 객체가 없도록 모든 작업이 끝나기를 기다린다.
    ~App() {
        try {
            context_.device().waitIdle();
        } catch (...) {
            // 소멸자에서는 예외를 밖으로 던지지 않는다.
        }
    }

    void run() {
        while (!window_.shouldClose()) {
            glfwPollEvents();
            if (window_.isKeyPressed(GLFW_KEY_ESCAPE)) window_.requestClose();
            renderer_.drawFrame();
        }
    }

private:
    // 선언 순서 = 생성 순서, 소멸은 역순.
    // 뒤의 객체가 앞의 객체를 참조하므로 이 순서를 바꾸면 안 된다.
    Window window_;
    VulkanContext context_;
    Swapchain swapchain_;
    Renderer renderer_;
};

std::optional<size_t> parseDeviceArg(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--device") return static_cast<size_t>(std::stoul(argv[i + 1]));
    }
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        App app(parseDeviceArg(argc, argv));
        app.run();
    } catch (const vk::SystemError& e) {
        std::cerr << "Vulkan error: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

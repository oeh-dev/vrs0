// Vulkan RAII 샘플
//
// 수명 스코프에 따라 나눈 구성:
//   Window         GLFW 초기화와 창
//   VulkanContext  앱 수명: 인스턴스, 디바이스, 큐 등
//   Swapchain      스왑체인 수명: 화면 크기가 바뀌면 다시 만드는 것들
//   Renderer       프레임 수명: 매 프레임 기록하고 제출하는 것들
//   Pipeline       셰이더와 그리기 설정을 묶은 그래픽스 파이프라인
//   Mesh           정점 버퍼와 인덱스 버퍼

#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <glm/gtc/matrix_transform.hpp>

#include "Mesh.hpp"
#include "Renderer.hpp"
#include "Swapchain.hpp"
#include "VulkanContext.hpp"
#include "Window.hpp"

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

// 사각형 하나. 정점 4개를 인덱스 6개(삼각형 2개)가 공유한다.
// 모델 좌표(x 오른쪽, y 위쪽 +, z 화면 앞쪽 +)이고, 앞(+z)에서 볼 때 반시계 방향으로 감는다.
const std::array<Vertex, 4> kQuadVertices{{
    {.pos = {-0.5f, -0.5f}, .color = {1.0f, 0.0f, 0.0f}},  // 왼쪽 아래
    {.pos = {0.5f, -0.5f}, .color = {0.0f, 1.0f, 0.0f}},   // 오른쪽 아래
    {.pos = {0.5f, 0.5f}, .color = {0.0f, 0.0f, 1.0f}},    // 오른쪽 위
    {.pos = {-0.5f, 0.5f}, .color = {1.0f, 1.0f, 1.0f}},   // 왼쪽 위
}};
const std::array<uint16_t, 6> kQuadIndices{0, 1, 2, 2, 3, 0};

class App {
public:
    explicit App(std::optional<size_t> requestedDevice)
        : window_(kWidth, kHeight, "Vulkan RAII Sample"),
          context_(window_.handle(), requestedDevice),
          swapchain_(context_, window_.handle()),
          mesh_(context_, kQuadVertices, kQuadIndices),
          renderer_(context_, swapchain_) {}

    // 멤버가 소멸하기 전에, GPU가 아직 사용 중인 객체가 없도록 모든 작업이 끝나기를 기다린다.
    ~App() {
        try {
            context_.device().waitIdle();
        } catch (const std::exception& e) {
            // 소멸자에서는 예외를 밖으로 던지지 않는다.
            std::cerr << "waitIdle failed during shutdown: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "waitIdle failed during shutdown: unknown error\n";
        }
    }

    void run() {
        while (!window_.shouldClose()) {
            glfwPollEvents();
            if (window_.isKeyPressed(GLFW_KEY_ESCAPE)) window_.requestClose();

            // 시간에 비례해 y축(세로축) 둘레로 회전한다. 프레임 속도와 상관없이 같은 빠르기가 된다.
            const float angle = static_cast<float>(glfwGetTime()) * glm::radians(60.0f);  // 초당 60도
            const glm::mat4 model = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));

            // 카메라: (0, 0, 2)에서 원점을 본다. 위쪽은 +y.
            const glm::mat4 view =
                glm::lookAt(glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f), glm::vec3(1.0f, 1.0f, 0.0f));

            renderer_.drawFrame(mesh_, model, view);
        }
    }

private:
    // 선언 순서 = 생성 순서, 소멸은 역순.
    // 뒤의 객체가 앞의 객체를 참조하므로 이 순서를 바꾸면 안 된다.
    Window window_;
    VulkanContext context_;
    Swapchain swapchain_;
    Mesh mesh_;
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

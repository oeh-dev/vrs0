// Vulkan RAII 샘플
//
// 수명 스코프에 따라 나눈 구성:
//   Window         GLFW 초기화와 창
//   VulkanContext  앱 수명: 인스턴스, 디바이스, 큐 등
//   Swapchain      스왑체인 수명: 화면 크기가 바뀌면 다시 만드는 것들
//   Renderer       프레임 수명: 매 프레임 기록하고 제출하는 것들
//   Pipeline       셰이더와 그리기 설정을 묶은 그래픽스 파이프라인
//   Mesh           정점 버퍼와 인덱스 버퍼 (Geometry가 만든 CPU 쪽 데이터를 GPU로 올린 것)
//
// 키: 1 smooth, 2 flat, 3 wireframe, Esc 종료

#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include "Geometry.hpp"
#include "Mesh.hpp"
#include "Renderer.hpp"
#include "Swapchain.hpp"
#include "VulkanContext.hpp"
#include "Window.hpp"

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

const glm::vec3 kTorchColor(1.0f, 0.55f, 0.25f);  // 횃불 같은 주황빛 (선형 값)

class App {
public:
    explicit App(std::optional<size_t> requestedDevice)
        : window_(kWidth, kHeight, "Vulkan RAII Sample"),
          context_(window_.handle(), requestedDevice),
          swapchain_(context_, window_.handle()),
          soccerBall_(context_, makeTruncatedIcosahedron(0.6f)),
          sphere_(context_, makeIcosphere(0.6f, 3, glm::vec3(0.8f, 0.25f, 0.1f))),
          lightMarker_(context_, makeIcosphere(0.05f, 2, kTorchColor)),
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

            // 셰이딩 모드 전환. 키를 누르고 있는 동안의 상태만 보는 임시 방식이다. (7단계에서 입력 처리를 정리한다)
            if (window_.isKeyPressed(GLFW_KEY_1)) mode_ = ShadingMode::Smooth;
            if (window_.isKeyPressed(GLFW_KEY_2)) mode_ = ShadingMode::Flat;
            if (window_.isKeyPressed(GLFW_KEY_3)) mode_ = ShadingMode::Wireframe;

            const float time = static_cast<float>(glfwGetTime());

            // 시간에 비례해 y축(세로축) 둘레로 돌리고, x축으로 조금 기울여 위쪽 면도 보이게 한다.
            // 행렬은 오른쪽부터 적용되므로 "먼저 y축 회전, 그다음 기울임, 마지막으로 제자리로 이동" 순서다.
            const float angle = time * glm::radians(60.0f);  // 초당 60도
            const glm::mat4 tilt = glm::rotate(glm::mat4(1.0f), glm::radians(20.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::mat4 spin = tilt * glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
            auto placeAt = [&](float x) { return glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)) * spin; };

            // 점광원: 두 물체 둘레를 수평으로 돈다. (반지름 1.6, 높이 0.5, 8초에 한 바퀴)
            const float orbit = time * glm::radians(45.0f);
            const PointLight light{
                .position = glm::vec3(1.6f * std::cos(orbit), 0.5f, 1.6f * std::sin(orbit)),
                .color = kTorchColor * 2.0f,
                .range = 4.0f,
            };

            const std::array<DrawItem, 3> items{{
                {.mesh = soccerBall_, .model = placeAt(-0.75f), .mode = mode_},
                {.mesh = sphere_, .model = placeAt(0.75f), .mode = mode_},
                // 광원 위치를 보여주는 작은 구. 스스로 빛나는 것처럼 조명 없이 칠한다.
                {.mesh = lightMarker_,
                 .model = glm::translate(glm::mat4(1.0f), light.position),
                 .mode = ShadingMode::Unlit},
            }};

            // 카메라: (0, 0, 3)에서 원점을 본다. 위쪽은 +y.
            const glm::mat4 view =
                glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

            renderer_.drawFrame(items, view, light);
        }
    }

private:
    // 선언 순서 = 생성 순서, 소멸은 역순.
    // 뒤의 객체가 앞의 객체를 참조하므로 이 순서를 바꾸면 안 된다.
    Window window_;
    VulkanContext context_;
    Swapchain swapchain_;
    Mesh soccerBall_;
    Mesh sphere_;
    Mesh lightMarker_;
    Renderer renderer_;

    ShadingMode mode_ = ShadingMode::Smooth;
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

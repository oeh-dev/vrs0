// Vulkan RAII 샘플
//
// 수명 스코프에 따라 나눈 구성:
//   Window         GLFW 초기화와 창, 입력
//   VulkanContext  앱 수명: 인스턴스, 디바이스, 큐 등
//   Swapchain      스왑체인 수명: 화면 크기가 바뀌면 다시 만드는 것들
//   Renderer       프레임 수명: 매 프레임 기록하고 제출하는 것들
//   Pipeline       셰이더와 그리기 설정을 묶은 그래픽스 파이프라인
//   Mesh           정점 버퍼와 인덱스 버퍼 (Geometry가 만든 CPU 쪽 데이터를 GPU로 올린 것)
//   App            장면 상태(물체, 선택, 카메라, 광원)와 입력 처리
//
// 키:
//   Tab / Shift+Tab  다음 / 이전 물체 선택
//   1 / 2 / 3        선택한 물체를 smooth / flat / wireframe으로
//   Space            선택한 물체의 회전을 멈추거나 다시 돌린다
//   방향키           카메라를 target 둘레로 돌린다
//   W / S            카메라가 다가간다 / 멀어진다
//   R                카메라를 처음 위치로
//   L                광원을 멈추거나 다시 움직인다
//   Esc              종료

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "Camera.hpp"
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

const float kTwoPi = 2.0f * glm::pi<float>();
const float kSpinSpeed = glm::radians(60.0f);   // 물체 회전, 초당
const float kLightSpeed = glm::radians(45.0f);  // 광원 궤도, 초당 (8초에 한 바퀴)
const float kOrbitSpeed = glm::radians(90.0f);  // 카메라 회전, 초당
const float kZoomSpeed = 1.0f;                  // 카메라 거리, 초당 e배

const OrbitCamera kInitialCamera{.pitch = glm::radians(10.0f)};

// 장면에 놓인 물체 하나의 상태.
// 이름은 영문으로 둔다. 8단계의 ImGui 기본 글꼴에는 한글 글리프가 없다.
struct SceneObject {
    const char* name;
    const Mesh& mesh;  // App이 소유한 메시. 물체보다 오래 산다.
    glm::vec3 position;
    ShadingMode mode = ShadingMode::Smooth;
    float angle = 0.0f;  // y축 회전 각도(라디안)
    bool spinning = true;

    // 모델 → 월드. 오른쪽부터 적용: y축 회전 → x축으로 기울임(위쪽 면도 보이게) → 제자리로 이동
    glm::mat4 model() const {
        const glm::mat4 tilt = glm::rotate(glm::mat4(1.0f), glm::radians(20.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        return glm::translate(glm::mat4(1.0f), position) * tilt *
               glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
    }
};

class App {
public:
    explicit App(std::optional<size_t> requestedDevice)
        : window_(kWidth, kHeight, "Vulkan RAII Sample"),
          context_(window_.handle(), requestedDevice),
          swapchain_(context_, window_.handle()),
          soccerBall_(context_, makeTruncatedIcosahedron(0.6f)),
          sphere_(context_, makeIcosphere(0.6f, 3, glm::vec3(0.8f, 0.25f, 0.1f))),
          lightMarker_(context_, makeIcosphere(0.05f, 2, kTorchColor)),
          selectionMarker_(context_, makeIcosahedron(0.06f, glm::vec3(0.9f))),
          renderer_(context_, swapchain_),
          objects_{{
              {.name = "Soccer ball", .mesh = soccerBall_, .position = {-0.75f, 0.0f, 0.0f}},
              {.name = "Sphere", .mesh = sphere_, .position = {0.75f, 0.0f, 0.0f}},
          }} {}

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
        printHelp();
        updateTitle();

        double last = window_.time();
        while (!window_.shouldClose()) {
            window_.pollEvents();

            // 프레임 사이에 흐른 시간. 움직임을 프레임 수가 아니라 시간에 비례시켜, 화면 갱신 빈도와 상관없이 같은 빠르기가 되게 한다.
            // 창을 끌거나 최소화했다 돌아오면 아주 커질 수 있으므로 상한을 두어 한 번에 크게 튀지 않게 한다.
            const double now = window_.time();
            const float dt = static_cast<float>(std::min(now - last, 0.1));
            last = now;

            handleKeyPresses();
            updateCamera(dt);
            updateScene(dt);
            draw(static_cast<float>(now));
        }
    }

private:
    // 한 번 누르면 한 번 일어나는 동작
    void handleKeyPresses() {
        bool changed = false;
        for (const Window::KeyPress& press : window_.takeKeyPresses()) {
            SceneObject& selected = objects_[selected_];
            switch (press.key) {
                case GLFW_KEY_ESCAPE:
                    window_.requestClose();
                    break;
                case GLFW_KEY_TAB: {
                    const size_t n = objects_.size();
                    selected_ = (press.mods & GLFW_MOD_SHIFT) ? (selected_ + n - 1) % n : (selected_ + 1) % n;
                    break;
                }
                case GLFW_KEY_1:
                    selected.mode = ShadingMode::Smooth;
                    break;
                case GLFW_KEY_2:
                    selected.mode = ShadingMode::Flat;
                    break;
                case GLFW_KEY_3:
                    if (context_.wireframeSupported()) {
                        selected.mode = ShadingMode::Wireframe;
                    } else {
                        std::cout << "Wireframe is not supported on this device\n";
                    }
                    break;
                case GLFW_KEY_SPACE:
                    selected.spinning = !selected.spinning;
                    break;
                case GLFW_KEY_R:
                    camera_ = kInitialCamera;
                    break;
                case GLFW_KEY_L:
                    lightMoving_ = !lightMoving_;
                    break;
                default:
                    continue;  // 쓰지 않는 키
            }
            changed = true;
        }
        if (changed) updateTitle();
    }

    // 누르고 있는 동안 계속되는 동작. 속도 × 시간만큼 움직인다.
    void updateCamera(float dt) {
        float yaw = 0.0f, pitch = 0.0f, zoom = 0.0f;
        if (window_.isKeyDown(GLFW_KEY_LEFT)) yaw -= 1.0f;
        if (window_.isKeyDown(GLFW_KEY_RIGHT)) yaw += 1.0f;
        if (window_.isKeyDown(GLFW_KEY_UP)) pitch += 1.0f;
        if (window_.isKeyDown(GLFW_KEY_DOWN)) pitch -= 1.0f;
        if (window_.isKeyDown(GLFW_KEY_W)) zoom -= 1.0f;
        if (window_.isKeyDown(GLFW_KEY_S)) zoom += 1.0f;

        camera_.orbit(yaw * kOrbitSpeed * dt, pitch * kOrbitSpeed * dt);
        camera_.zoom(std::exp(zoom * kZoomSpeed * dt));
    }

    void updateScene(float dt) {
        // 각도는 한 바퀴마다 되돌려, 오래 실행해도 값이 커져 float 정밀도가 떨어지지 않게 한다.
        for (SceneObject& obj : objects_) {
            if (obj.spinning) obj.angle = std::fmod(obj.angle + kSpinSpeed * dt, kTwoPi);
        }
        if (lightMoving_) lightAngle_ = std::fmod(lightAngle_ + kLightSpeed * dt, kTwoPi);
    }

    void draw(float time) {
        // 점광원: 두 물체 둘레를 수평으로 돈다. (반지름 1.6, 높이 0.5)
        const PointLight light{
            .position = glm::vec3(1.6f * std::cos(lightAngle_), 0.5f, 1.6f * std::sin(lightAngle_)),
            .color = kTorchColor * 2.0f,
            .range = 4.0f,
        };

        std::vector<DrawItem> items;
        for (const SceneObject& obj : objects_) {
            items.push_back(DrawItem{.mesh = obj.mesh, .model = obj.model(), .mode = obj.mode});
        }
        // 광원 위치를 보여주는 작은 구. 스스로 빛나는 것처럼 조명 없이 칠한다.
        items.push_back(DrawItem{
            .mesh = lightMarker_,
            .model = glm::translate(glm::mat4(1.0f), light.position),
            .mode = ShadingMode::Unlit,
        });
        // 선택 표시: 선택한 물체 위에 떠서 위아래로 천천히 움직이는 작은 다면체
        const float bob = 0.04f * std::sin(time * 3.0f);
        items.push_back(DrawItem{
            .mesh = selectionMarker_,
            .model = glm::translate(glm::mat4(1.0f), objects_[selected_].position + glm::vec3(0.0f, 0.85f + bob, 0.0f)),
            .mode = ShadingMode::Unlit,
        });

        renderer_.drawFrame(items, camera_.view(), light);
    }

    // 상태 표시. 8단계에서 ImGui로 옮기기 전까지 창 제목을 쓴다.
    void updateTitle() {
        const SceneObject& selected = objects_[selected_];
        std::string title = std::string("Vulkan RAII Sample | ") + selected.name + " [" + toString(selected.mode) + "]";
        if (!selected.spinning) title += " (stopped)";
        if (!lightMoving_) title += " | light stopped";
        window_.setTitle(title);
    }

    static void printHelp() {
        std::cout << "Keys:\n"
                     "  Tab / Shift+Tab  select next / previous object\n"
                     "  1 / 2 / 3        smooth / flat / wireframe (selected object)\n"
                     "  Space            stop / start spinning (selected object)\n"
                     "  Arrow keys       orbit camera\n"
                     "  W / S            move camera closer / farther\n"
                     "  R                reset camera\n"
                     "  L                stop / start light\n"
                     "  Esc              quit\n";
    }

    // 선언 순서 = 생성 순서, 소멸은 역순.
    // 뒤의 객체가 앞의 객체를 참조하므로 이 순서를 바꾸면 안 된다.
    Window window_;
    VulkanContext context_;
    Swapchain swapchain_;
    Mesh soccerBall_;
    Mesh sphere_;
    Mesh lightMarker_;
    Mesh selectionMarker_;
    Renderer renderer_;

    // 장면 상태 (GPU 자원이 아니므로 순서 제약은 메시보다 뒤라는 것뿐)
    std::array<SceneObject, 2> objects_;
    size_t selected_ = 0;
    OrbitCamera camera_ = kInitialCamera;
    float lightAngle_ = 0.0f;
    bool lightMoving_ = true;
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

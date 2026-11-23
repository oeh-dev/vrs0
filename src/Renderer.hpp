#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "Buffer.hpp"
#include "Pipeline.hpp"
#include "vk.hpp"

class VulkanContext;
class Swapchain;
class Mesh;
struct SwapchainImage;
struct DepthBuffer;

// 물체를 어떻게 칠할 것인가. 모드마다 파이프라인이 하나씩 있다.
enum class ShadingMode {
    Smooth,     // 정점 법선 보간 + 조명
    Flat,       // 면 법선 + 조명
    Wireframe,  // 모서리만 (지원하지 않는 디바이스에서는 Smooth로 대신한다)
    Unlit,      // 조명 없이 정점 색 그대로
};

// 그릴 물체 하나: 어떤 메시를 어디에(모델 → 월드) 어떻게 칠하는가.
// mesh는 그리는 동안 살아 있어야 한다. 장면 데이터는 App이 소유하고 매 프레임 넘겨준다.
struct DrawItem {
    const Mesh& mesh;
    glm::mat4 model;
    ShadingMode mode;
};

// 점광원 하나. 위치는 월드 좌표.
struct PointLight {
    glm::vec3 position;
    glm::vec3 color;  // 색 × 세기
    float range;      // 이 거리에서 영향이 0이 된다.
};

// 프레임 슬롯 하나에 딸린 객체들. CPU가 GPU보다 앞서 준비하는 프레임마다 하나씩 둔다.
// 매 프레임 CPU가 고쳐 쓰는 자원은 모두 여기 둔다. GPU가 앞 프레임을 그리며 읽고 있는 것을 덮어쓰지 않도록.
struct FrameData {
    vk::raii::CommandBuffer commandBuffer;
    vk::raii::Semaphore imageAvailable;  // 이미지 획득 → 그리기
    vk::raii::Fence inFlight;            // GPU 작업 완료 → CPU
    GpuBuffer uniforms;                  // FrameUniforms 하나. CPU가 매 프레임 쓴다.
    void* uniformsMapped;                // 계속 매핑해 둔 주소. 메모리가 해제될 때 매핑도 함께 풀린다.
    vk::raii::DescriptorSet descriptorSet;  // uniforms를 셰이더의 set 0, binding 0에 연결한다.
};

// 프레임 수명 스코프: 매 프레임 명령을 기록하고 제출한다.
class Renderer {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    // context와 swapchain이 이 객체보다 오래 살아야 한다. (App에서 보장)
    Renderer(const VulkanContext& context, Swapchain& swapchain);

    // view: 월드 → 카메라. 투영은 화면 비율을 아는 Renderer가 만든다.
    void drawFrame(std::span<const DrawItem> items, const glm::mat4& view, const PointLight& light);

private:
    // 어느 이미지에 그릴지만 알면 되므로, 인덱스가 아니라 이미지와 크기를 받는다.
    void recordCommands(const vk::raii::CommandBuffer& cmd, const SwapchainImage& target, const DepthBuffer& depth,
                        vk::Extent2D extent, const vk::raii::DescriptorSet& descriptorSet,
                        std::span<const DrawItem> items, const glm::mat4& viewProj);

    Pipeline makePipeline(const PipelineOptions& options) const;
    const Pipeline& pipelineFor(ShadingMode mode) const;

    const VulkanContext& context_;
    Swapchain& swapchain_;

    // 선언 순서 = 생성 순서, 소멸은 역순.
    // 레이아웃은 그것을 쓰는 파이프라인보다, 풀은 거기서 할당한 객체(frames_ 안)보다 앞에 둔다.
    vk::raii::DescriptorSetLayout descriptorSetLayout_;
    vk::raii::PipelineLayout pipelineLayout_;  // 모든 파이프라인이 공유한다.
    Pipeline smoothPipeline_;
    Pipeline flatPipeline_;
    Pipeline unlitPipeline_;
    std::optional<Pipeline> wireframePipeline_;  // 디바이스가 fillModeNonSolid를 지원할 때만 만든다.
    vk::raii::CommandPool commandPool_ = nullptr;
    vk::raii::DescriptorPool descriptorPool_ = nullptr;
    std::vector<FrameData> frames_;
    uint32_t frame_ = 0;
};

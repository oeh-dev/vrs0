#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

#include "Pipeline.hpp"
#include "vk.hpp"

class VulkanContext;
class Swapchain;
class Mesh;
struct SwapchainImage;

// 프레임 슬롯 하나에 딸린 객체들. CPU가 GPU보다 앞서 준비하는 프레임마다 하나씩 둔다.
struct FrameData {
    vk::raii::CommandBuffer commandBuffer;
    vk::raii::Semaphore imageAvailable;  // 이미지 획득 → 그리기
    vk::raii::Fence inFlight;            // GPU 작업 완료 → CPU
};

// 프레임 수명 스코프: 매 프레임 명령을 기록하고 제출한다.
class Renderer {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    // context와 swapchain이 이 객체보다 오래 살아야 한다. (App에서 보장)
    Renderer(const VulkanContext& context, Swapchain& swapchain);

    // mesh는 그리는 동안 살아 있어야 한다. 장면 데이터는 App이 소유하고 매 프레임 넘겨준다.
    // model: 모델 → 월드, view: 월드 → 카메라. 투영은 화면 비율을 아는 Renderer가 만든다.
    void drawFrame(const Mesh& mesh, const glm::mat4& model, const glm::mat4& view);

private:
    // 어느 이미지에 그릴지만 알면 되므로, 인덱스가 아니라 이미지와 크기를 받는다.
    void recordCommands(const vk::raii::CommandBuffer& cmd, const SwapchainImage& target, vk::Extent2D extent,
                        const Mesh& mesh, const glm::mat4& mvp);

    const VulkanContext& context_;
    Swapchain& swapchain_;

    Pipeline pipeline_;
    vk::raii::CommandPool commandPool_ = nullptr;
    std::vector<FrameData> frames_;  // 커맨드 풀보다 뒤에 선언: 커맨드 버퍼가 풀보다 먼저 소멸
    uint32_t frame_ = 0;
};

#pragma once

#include <cstdint>
#include <vector>

#include "vk.hpp"

class VulkanContext;
class Swapchain;

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

    void drawFrame();

private:
    void recordCommands(const vk::raii::CommandBuffer& cmd, uint32_t imageIndex);

    const VulkanContext& context_;
    Swapchain& swapchain_;

    vk::raii::CommandPool commandPool_ = nullptr;
    std::vector<FrameData> frames_;  // 커맨드 풀보다 뒤에 선언: 커맨드 버퍼가 풀보다 먼저 소멸
    uint32_t frame_ = 0;
};

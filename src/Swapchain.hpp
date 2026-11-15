#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "vk.hpp"

class VulkanContext;

// 스왑체인 이미지 하나에 딸린 객체들. imageIndex로 접근한다.
struct SwapchainImage {
    vk::Image image;                    // 스왑체인이 소유. 직접 정리하지 않는다.
    vk::raii::ImageView view;           // 렌더링 대상으로 지정할 때 쓴다.
    vk::raii::Semaphore renderFinished; // 그리기 완료 → 화면 출력
};

// 깊이 버퍼. 스왑체인 이미지와 크기가 같아야 하므로 스왑체인과 함께 다시 만든다.
// 프레임마다 지우고 다 그린 뒤 버리므로, 프레임이 여럿이어도 하나를 같이 쓴다. (Renderer의 배리어가 순서를 보장)
struct DepthBuffer {
    // 소멸은 역순: 뷰 → 이미지 → 메모리
    vk::raii::DeviceMemory memory = nullptr;
    vk::raii::Image image = nullptr;
    vk::raii::ImageView view = nullptr;
};

// 스왑체인 수명 스코프: 화면 크기나 디스플레이가 바뀌면 통째로 다시 만드는 객체들
class Swapchain {
public:
    Swapchain(const VulkanContext& context, GLFWwindow* window);

    // 그릴 이미지를 받는다. 이미지가 준비되면 signal이 신호된다.
    // 스왑체인이 무효화되었으면 재생성하고 nullopt를 반환한다. (이번 프레임은 건너뛴다)
    std::optional<uint32_t> acquireNextImage(const vk::raii::Semaphore& signal);

    // 해당 이미지의 renderFinished가 신호된 뒤 화면에 출력한다. 필요하면 재생성한다.
    void present(uint32_t imageIndex);

    const SwapchainImage& image(uint32_t index) const { return images_[index]; }
    vk::Extent2D extent() const { return extent_; }
    vk::Format format() const { return format_; }
    const DepthBuffer& depth() const { return depth_; }
    vk::Format depthFormat() const { return depthFormat_; }

private:
    void create();
    void recreate();
    DepthBuffer createDepthBuffer() const;

    const VulkanContext& context_;  // VulkanContext가 이 객체보다 오래 살아야 한다. (App에서 보장)
    GLFWwindow* window_;

    vk::raii::SwapchainKHR swapchain_ = nullptr;
    vk::Format format_ = vk::Format::eUndefined;
    vk::Extent2D extent_;
    std::vector<SwapchainImage> images_;  // 스왑체인보다 뒤에 선언: 스왑체인보다 먼저 소멸
    vk::Format depthFormat_ = vk::Format::eUndefined;  // 재생성해도 바뀌지 않는다. (파이프라인이 이 포맷에 맞춰 만들어짐)
    DepthBuffer depth_;
};

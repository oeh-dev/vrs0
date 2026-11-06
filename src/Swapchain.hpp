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

private:
    void create();
    void recreate();

    const VulkanContext& context_;  // VulkanContext가 이 객체보다 오래 살아야 한다. (App에서 보장)
    GLFWwindow* window_;

    vk::raii::SwapchainKHR swapchain_ = nullptr;
    vk::Format format_ = vk::Format::eUndefined;
    vk::Extent2D extent_;
    std::vector<SwapchainImage> images_;  // 스왑체인보다 뒤에 선언: 스왑체인보다 먼저 소멸
};

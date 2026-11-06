#include "Swapchain.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "VulkanContext.hpp"

namespace {

// 바이트 순서와 상관없이 sRGB 포맷을 우선한다. 셰이더 값이 곧 물리적인 빛의 세기가 되도록.
vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (vk::Format preferred : {vk::Format::eB8G8R8A8Srgb, vk::Format::eR8G8B8A8Srgb}) {
        for (const auto& f : formats) {
            if (f.format == preferred && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) return f;
        }
    }
    std::cerr << "Warning: no sRGB swapchain format, colors may look darker\n";
    return formats.front();
}

vk::CompositeAlphaFlagBitsKHR chooseCompositeAlpha(vk::CompositeAlphaFlagsKHR supported) {
    for (auto bit : {vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::CompositeAlphaFlagBitsKHR::eInherit,
                     vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                     vk::CompositeAlphaFlagBitsKHR::ePostMultiplied}) {
        if (supported & bit) return bit;
    }
    throw std::runtime_error("No supported composite alpha mode");
}

}  // namespace

Swapchain::Swapchain(const VulkanContext& context, GLFWwindow* window) : context_(context), window_(window) {
    create();
}

void Swapchain::create() {
    const auto& pd = context_.physicalDevice();
    const auto& surface = context_.surface();
    const auto caps = pd.getSurfaceCapabilitiesKHR(*surface);
    const auto surfaceFormat = chooseSurfaceFormat(pd.getSurfaceFormatsKHR(*surface));

    // currentExtent가 특수값(0xFFFFFFFF)이면 창의 픽셀 크기로 직접 정한다.
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        extent_ = caps.currentExtent;
    } else {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        extent_ = vk::Extent2D{
            .width = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width),
            .height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height),
        };
    }

    // 최소 개수보다 하나 더 요청해 드라이버를 기다리는 일을 줄인다. (최대값 0은 제한 없음)
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    const vk::SwapchainCreateInfoKHR info{
        .surface = *surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent_,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,  // 큐 패밀리 하나만 사용
        .preTransform = caps.currentTransform,
        .compositeAlpha = chooseCompositeAlpha(caps.supportedCompositeAlpha),
        .presentMode = vk::PresentModeKHR::eFifo,  // 수직 동기화. 모든 구현이 반드시 지원한다.
        .clipped = VK_TRUE,
        .oldSwapchain = *swapchain_,  // 재생성 시 이전 스왑체인 (처음에는 VK_NULL_HANDLE)
    };
    // 이전 이미지 뷰와 세마포어를 먼저 정리한다. (이전 스왑체인의 이미지를 가리키므로)
    images_.clear();
    // 새 스왑체인을 만든 뒤 대입하므로, 이전 스왑체인은 생성이 끝난 다음에 소멸한다.
    swapchain_ = vk::raii::SwapchainKHR(context_.device(), info);
    format_ = surfaceFormat.format;

    for (vk::Image image : swapchain_.getImages()) {
        images_.push_back(SwapchainImage{
            .image = image,
            .view = vk::raii::ImageView(context_.device(),
                                        vk::ImageViewCreateInfo{
                                            .image = image,
                                            .viewType = vk::ImageViewType::e2D,
                                            .format = format_,
                                            .subresourceRange =
                                                {
                                                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                                                    .baseMipLevel = 0,
                                                    .levelCount = 1,
                                                    .baseArrayLayer = 0,
                                                    .layerCount = 1,
                                                },
                                        }),
            .renderFinished = vk::raii::Semaphore(context_.device(), vk::SemaphoreCreateInfo{}),
        });
    }
    std::cout << "Swapchain: " << extent_.width << 'x' << extent_.height << ", " << images_.size() << " images, "
              << vk::to_string(format_) << '\n';
}

void Swapchain::recreate() {
    // 창이 최소화되어 크기가 0이면 복원될 때까지 기다린다.
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    while ((w == 0 || h == 0) && !glfwWindowShouldClose(window_)) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &w, &h);
    }
    context_.device().waitIdle();  // 이전 이미지를 쓰는 GPU 작업이 끝나기를 기다린다.
    create();
}

std::optional<uint32_t> Swapchain::acquireNextImage(const vk::raii::Semaphore& signal) {
    const auto acquired = swapchain_.acquireNextImage(std::numeric_limits<uint64_t>::max(), *signal);
    if (acquired.result == vk::Result::eErrorOutOfDateKHR) {
        recreate();
        return std::nullopt;
    }
    return acquired.value;  // eSuboptimalKHR이면 이번 프레임은 그리고, present에서 재생성한다.
}

void Swapchain::present(uint32_t imageIndex) {
    const vk::Semaphore wait = *images_[imageIndex].renderFinished;
    const vk::SwapchainKHR swapchain = *swapchain_;
    const vk::Result result = context_.queue().presentKHR(vk::PresentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &wait,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &imageIndex,
    });
    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
        recreate();
    }
}

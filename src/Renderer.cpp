#include "Renderer.hpp"

#include <array>
#include <cmath>
#include <limits>

#include "Swapchain.hpp"
#include "VulkanContext.hpp"

namespace {

// 이미지 전체(색상, 밉 0, 레이어 0)를 가리키는 범위
constexpr vk::ImageSubresourceRange kColorRange{
    .aspectMask = vk::ImageAspectFlagBits::eColor,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
};

// synchronization2 방식의 이미지 배리어 하나를 기록한다.
//  - 레이아웃 전환: oldLayout → newLayout
//  - 실행 순서: 앞 명령의 srcStage가 끝난 뒤 뒤 명령의 dstStage가 시작된다.
//  - 메모리 가시성: srcAccess로 쓴 결과가 dstAccess에서 보이게 한다.
void transitionImage(const vk::raii::CommandBuffer& cmd, vk::Image image, vk::ImageLayout oldLayout,
                     vk::ImageLayout newLayout, vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess,
                     vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess,
                     const vk::ImageSubresourceRange& range = kColorRange) {
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,  // 큐 패밀리 간 소유권 이전 없음
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = range,
    };
    cmd.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });
}

}  // namespace

Renderer::Renderer(const VulkanContext& context, Swapchain& swapchain) : context_(context), swapchain_(swapchain) {
    const auto& device = context_.device();

    commandPool_ = vk::raii::CommandPool(
        device, vk::CommandPoolCreateInfo{
                    .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,  // 버퍼를 개별로 다시 기록
                    .queueFamilyIndex = context_.queueFamily(),
                });
    auto commandBuffers = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
        .commandPool = *commandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = kMaxFramesInFlight,
    });

    for (auto& cb : commandBuffers) {
        frames_.push_back(FrameData{
            .commandBuffer = std::move(cb),
            .imageAvailable = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo{}),
            // 첫 프레임에서 기다리지 않도록 신호된 상태로 만든다.
            .inFlight = vk::raii::Fence(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled}),
        });
    }
}

void Renderer::drawFrame() {
    const auto& device = context_.device();
    FrameData& frame = frames_[frame_];

    // 1. 이 프레임 슬롯을 마지막으로 쓴 GPU 작업이 끝나기를 기다린다.
    (void)device.waitForFences(*frame.inFlight, VK_TRUE, std::numeric_limits<uint64_t>::max());

    // 2. 그릴 스왑체인 이미지를 받는다. 이미지가 준비되면 imageAvailable이 신호된다.
    const auto imageIndex = swapchain_.acquireNextImage(frame.imageAvailable);
    if (!imageIndex) return;  // 펜스를 리셋하지 않았으므로 다음 프레임에서 다시 기다려도 멈추지 않는다.
    device.resetFences(*frame.inFlight);

    // 3. 명령을 기록한다.
    frame.commandBuffer.reset();
    recordCommands(frame.commandBuffer, *imageIndex);

    // 4. 큐에 제출한다. imageAvailable을 기다렸다가 실행하고, 끝나면 renderFinished를 신호한다.
    const vk::SemaphoreSubmitInfo waitInfo{
        .semaphore = *frame.imageAvailable,
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    };
    const vk::CommandBufferSubmitInfo cmdInfo{.commandBuffer = *frame.commandBuffer};
    const vk::SemaphoreSubmitInfo signalInfo{
        .semaphore = *swapchain_.image(*imageIndex).renderFinished,
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    };
    context_.queue().submit2(vk::SubmitInfo2{
                                 .waitSemaphoreInfoCount = 1,
                                 .pWaitSemaphoreInfos = &waitInfo,
                                 .commandBufferInfoCount = 1,
                                 .pCommandBufferInfos = &cmdInfo,
                                 .signalSemaphoreInfoCount = 1,
                                 .pSignalSemaphoreInfos = &signalInfo,
                             },
                             *frame.inFlight);  // GPU 작업이 끝나면 펜스가 신호된다.

    // 5. 화면 출력을 요청한다. renderFinished가 신호된 뒤에 출력된다.
    swapchain_.present(*imageIndex);

    frame_ = (frame_ + 1) % kMaxFramesInFlight;
}

void Renderer::recordCommands(const vk::raii::CommandBuffer& cmd, uint32_t imageIndex) {
    const SwapchainImage& target = swapchain_.image(imageIndex);

    cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // 이미지 레이아웃 전환: 이전 내용은 버리고(Undefined) 색상 출력용으로.
    // srcStage를 이미지 획득 세마포어의 대기 단계와 맞춰, 이미지가 넘어온 뒤에 전환되게 한다.
    transitionImage(cmd, target.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput, {},
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);

    // 시간에 따라 천천히 바뀌는 색으로 지운다. 색이 변하면 프레임 루프가 돌고 있다는 뜻이다.
    // sRGB 포맷이므로 이 값은 선형(물리적인 빛의 세기)으로 해석된다.
    const float t = static_cast<float>(glfwGetTime());
    const vk::ClearValue clear{.color = vk::ClearColorValue{.float32 = std::array<float, 4>{
                                                                0.5f + 0.5f * std::sin(t),
                                                                0.5f + 0.5f * std::sin(t + 2.1f),
                                                                0.5f + 0.5f * std::sin(t + 4.2f),
                                                                1.0f,
                                                            }}};
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = *target.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clear,
    };
    // dynamic rendering: 렌더패스와 프레임버퍼 객체 없이 그 자리에서 대상을 지정한다.
    cmd.beginRendering(vk::RenderingInfo{
        .renderArea = vk::Rect2D{.offset = {0, 0}, .extent = swapchain_.extent()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    });
    // 4단계부터 여기에 그리기 명령이 들어간다.
    cmd.endRendering();

    // 이미지 레이아웃 전환: 색상 출력용에서 화면 출력용으로.
    // 화면 출력은 파이프라인 밖이므로 dstStage는 BottomOfPipe, 가시성은 renderFinished 세마포어가 보장한다.
    transitionImage(cmd, target.image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::PipelineStageFlagBits2::eBottomOfPipe, {});

    cmd.end();
}

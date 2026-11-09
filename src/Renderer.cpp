#include "Renderer.hpp"

#include <array>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

#include "Mesh.hpp"
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

// 카메라 공간 → Vulkan 클립 공간.
// GLM은 OpenGL 관례(y가 위쪽 +)를 따르므로 y를 뒤집어 Vulkan 관례(y가 아래쪽 +)에 맞춘다.
// 깊이 범위 0..1은 CMake의 GLM_FORCE_DEPTH_ZERO_TO_ONE으로 맞춘다.
glm::mat4 makeProjection(vk::Extent2D extent) {
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);  // 세로 시야각, 비율, near, far
    proj[1][1] *= -1.0f;
    return proj;
}

}  // namespace

Renderer::Renderer(const VulkanContext& context, Swapchain& swapchain)
    : context_(context),
      swapchain_(swapchain),
      // 셰이더 경로는 CMake가 SHADER_DIR로 넘겨준다.
      // 파이프라인은 스왑체인 포맷에 맞춰 만든다. (재생성 시 포맷이 바뀌는 경우는 다루지 않는다)
      pipeline_(context.device(), swapchain.format(), SHADER_DIR "/triangle.vert.spv",
                SHADER_DIR "/triangle.frag.spv") {
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

void Renderer::drawFrame(const Mesh& mesh, const glm::mat4& model, const glm::mat4& view) {
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
    // 이미지를 받은 뒤에 투영을 만든다. 획득 과정에서 스왑체인이 재생성되면 크기가 바뀌기 때문이다.
    const vk::Extent2D extent = swapchain_.extent();
    const glm::mat4 mvp = makeProjection(extent) * view * model;  // 오른쪽부터 적용: 모델 → 월드 → 카메라 → 클립
    recordCommands(frame.commandBuffer, swapchain_.image(*imageIndex), extent, mesh, mvp);

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

void Renderer::recordCommands(const vk::raii::CommandBuffer& cmd, const SwapchainImage& target,
                              vk::Extent2D extent, const Mesh& mesh, const glm::mat4& mvp) {
    cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // 이미지 레이아웃 전환: 이전 내용은 버리고(Undefined) 색상 출력용으로.
    // srcStage를 이미지 획득 세마포어의 대기 단계와 맞춰, 이미지가 넘어온 뒤에 전환되게 한다.
    transitionImage(cmd, target.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput, {},
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);

    // 도형이 잘 보이도록 어두운 단색으로 지운다. (sRGB 포맷이므로 선형 값)
    const vk::ClearValue clear{.color = vk::ClearColorValue{.float32 = std::array<float, 4>{0.01f, 0.01f, 0.02f, 1.0f}}};
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = *target.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clear,
    };
    // dynamic rendering: 렌더패스와 프레임버퍼 객체 없이 그 자리에서 대상을 지정한다.
    cmd.beginRendering(vk::RenderingInfo{
        .renderArea = vk::Rect2D{.offset = {0, 0}, .extent = extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    });

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline_.handle());

    // 파이프라인에서 동적으로 남겨둔 뷰포트와 시저를 지정한다. 이미지 전체에 그린다.
    cmd.setViewport(0, vk::Viewport{
                           .x = 0.0f,
                           .y = 0.0f,
                           .width = static_cast<float>(extent.width),
                           .height = static_cast<float>(extent.height),
                           .minDepth = 0.0f,
                           .maxDepth = 1.0f,
                       });
    cmd.setScissor(0, vk::Rect2D{.offset = {0, 0}, .extent = extent});

    // 행렬을 push constant로 보낸다. 바인딩된 파이프라인의 레이아웃과 같은 범위여야 한다.
    const PushConstants pushConstants{.mvp = mvp};
    cmd.pushConstants<PushConstants>(*pipeline_.layout(), vk::ShaderStageFlagBits::eVertex, 0, pushConstants);    

    // 정점/인덱스 버퍼를 바인딩하고 그린다.
    mesh.record(cmd);

    cmd.endRendering();

    // 이미지 레이아웃 전환: 색상 출력용에서 화면 출력용으로.
    // 화면 출력은 파이프라인 밖이므로 dstStage는 BottomOfPipe, 가시성은 renderFinished 세마포어가 보장한다.
    transitionImage(cmd, target.image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::PipelineStageFlagBits2::eBottomOfPipe, {});

    cmd.end();
}

#include "Renderer.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

#include "Mesh.hpp"
#include "ShaderInterface.hpp"
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

// 깊이 이미지 전체를 가리키는 범위. 깊이 전용 포맷이라 스텐실은 포함하지 않는다.
constexpr vk::ImageSubresourceRange kDepthRange{
    .aspectMask = vk::ImageAspectFlagBits::eDepth,
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

// 디스크립터 세트 레이아웃: 셰이더가 set 0에서 무엇을 받는가. binding 0에 프래그먼트 셰이더가 읽는 uniform buffer 하나.
vk::raii::DescriptorSetLayout createDescriptorSetLayout(const vk::raii::Device& device) {
    const vk::DescriptorSetLayoutBinding binding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
    };
    return vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo{
                                                     .bindingCount = 1,
                                                     .pBindings = &binding,
                                                 });
}

// 파이프라인 레이아웃: 디스크립터 세트 레이아웃들과 push constant 범위를 묶은 것.
vk::raii::PipelineLayout createPipelineLayout(const vk::raii::Device& device,
                                              const vk::raii::DescriptorSetLayout& setLayout) {
    // push constant는 버텍스 셰이더에서만 읽는다.
    const vk::PushConstantRange pushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    const vk::DescriptorSetLayout setLayoutHandle = *setLayout;
    return vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo{
                                                .setLayoutCount = 1,
                                                .pSetLayouts = &setLayoutHandle,
                                                .pushConstantRangeCount = 1,
                                                .pPushConstantRanges = &pushConstantRange,
                                            });
}

}  // namespace

Renderer::Renderer(const VulkanContext& context, Swapchain& swapchain)
    : context_(context),
      swapchain_(swapchain),
      descriptorSetLayout_(createDescriptorSetLayout(context.device())),
      pipelineLayout_(createPipelineLayout(context.device(), descriptorSetLayout_)),
      smoothPipeline_(makePipeline({.fragmentMode = FragmentMode::kSmooth})),
      flatPipeline_(makePipeline({.fragmentMode = FragmentMode::kFlat})),
      unlitPipeline_(makePipeline({.fragmentMode = FragmentMode::kUnlit})) {
    const auto& device = context_.device();

    if (context_.wireframeSupported()) {
        wireframePipeline_.emplace(
            makePipeline({.polygonMode = vk::PolygonMode::eLine, .fragmentMode = FragmentMode::kWireframe}));
    } else {
        std::cerr << "Warning: wireframe not supported, falling back to smooth shading\n";
    }

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

    // 디스크립터 풀: 디스크립터 세트를 할당할 공간. 프레임 슬롯마다 uniform buffer 디스크립터 하나씩.
    const vk::DescriptorPoolSize poolSize{
        .type = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = kMaxFramesInFlight,
    };
    descriptorPool_ = vk::raii::DescriptorPool(
        device, vk::DescriptorPoolCreateInfo{
                    // vk::raii::DescriptorSet은 소멸할 때 세트를 하나씩 해제(vkFreeDescriptorSets)하므로
                    // 개별 해제를 허용하는 이 플래그가 필요하다. 없으면 종료 시 검증 레이어가 오류를 낸다.
                    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                    .maxSets = kMaxFramesInFlight,
                    .poolSizeCount = 1,
                    .pPoolSizes = &poolSize,
                });
    const std::vector<vk::DescriptorSetLayout> setLayouts(kMaxFramesInFlight, *descriptorSetLayout_);
    auto descriptorSets = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{
        .descriptorPool = *descriptorPool_,
        .descriptorSetCount = kMaxFramesInFlight,
        .pSetLayouts = setLayouts.data(),
    });

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        // 매 프레임 CPU가 쓰는 작은 버퍼라 스테이징 없이 CPU가 쓸 수 있는 메모리에 둔다.
        GpuBuffer uniforms =
            createBuffer(context_, sizeof(FrameUniforms), vk::BufferUsageFlagBits::eUniformBuffer,
                         vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        void* mapped = uniforms.memory.mapMemory(0, sizeof(FrameUniforms));  // 매핑한 채로 계속 쓴다.

        // 세트의 binding 0이 이 버퍼를 가리키게 한다. 버퍼는 바뀌지 않으므로 한 번만 하면 된다.
        const vk::DescriptorBufferInfo bufferInfo{
            .buffer = *uniforms.buffer,
            .offset = 0,
            .range = sizeof(FrameUniforms),
        };
        device.updateDescriptorSets(vk::WriteDescriptorSet{
                                        .dstSet = *descriptorSets[i],
                                        .dstBinding = 0,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                                        .pBufferInfo = &bufferInfo,
                                    },
                                    nullptr);

        frames_.push_back(FrameData{
            .commandBuffer = std::move(commandBuffers[i]),
            .imageAvailable = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo{}),
            // 첫 프레임에서 기다리지 않도록 신호된 상태로 만든다.
            .inFlight = vk::raii::Fence(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled}),
            .uniforms = std::move(uniforms),
            .uniformsMapped = mapped,
            .descriptorSet = std::move(descriptorSets[i]),
        });
    }
}

Pipeline Renderer::makePipeline(const PipelineOptions& options) const {
    // 셰이더 경로는 CMake가 SHADER_DIR로 넘겨준다. 모든 변형이 같은 셰이더를 쓰고 특수화 상수만 다르다.
    // 파이프라인은 스왑체인 포맷에 맞춰 만든다. (재생성 시 포맷이 바뀌는 경우는 다루지 않는다)
    return Pipeline(context_.device(), pipelineLayout_, swapchain_.format(), swapchain_.depthFormat(),
                    SHADER_DIR "/lit.vert.spv", SHADER_DIR "/lit.frag.spv", options);
}

const Pipeline& Renderer::pipelineFor(ShadingMode mode) const {
    switch (mode) {
        case ShadingMode::Smooth: return smoothPipeline_;
        case ShadingMode::Flat: return flatPipeline_;
        case ShadingMode::Wireframe: return wireframePipeline_ ? *wireframePipeline_ : smoothPipeline_;
        case ShadingMode::Unlit: return unlitPipeline_;
    }
    return smoothPipeline_;
}

void Renderer::drawFrame(std::span<const DrawItem> items, const glm::mat4& view, const PointLight& light) {
    const auto& device = context_.device();
    FrameData& frame = frames_[frame_];

    // 1. 이 프레임 슬롯을 마지막으로 쓴 GPU 작업이 끝나기를 기다린다.
    (void)device.waitForFences(*frame.inFlight, VK_TRUE, std::numeric_limits<uint64_t>::max());

    // 2. 그릴 스왑체인 이미지를 받는다. 이미지가 준비되면 imageAvailable이 신호된다.
    const auto imageIndex = swapchain_.acquireNextImage(frame.imageAvailable);
    if (!imageIndex) return;  // 펜스를 리셋하지 않았으므로 다음 프레임에서 다시 기다려도 멈추지 않는다.
    device.resetFences(*frame.inFlight);

    // 3. 이 슬롯의 uniform buffer를 채운다. 1에서 펜스를 기다렸으므로 GPU는 더 이상 이 버퍼를 읽지 않는다.
    //    HOST_COHERENT이고, 제출(submit)이 그 전의 호스트 쓰기를 GPU에 보이게 하므로 flush나 배리어는 필요 없다.
    const FrameUniforms uniforms{
        .cameraPos = glm::inverse(view)[3],  // 뷰 행렬의 역행렬은 카메라 → 월드. 그 이동 성분이 카메라 위치다.
        .lightPos = glm::vec4(light.position, light.range),
        .lightColor = glm::vec4(light.color, 0.0f),
    };
    std::memcpy(frame.uniformsMapped, &uniforms, sizeof(uniforms));

    // 4. 명령을 기록한다.
    frame.commandBuffer.reset();
    // 이미지를 받은 뒤에 투영을 만든다. 획득 과정에서 스왑체인이 재생성되면 크기가 바뀌기 때문이다.
    const vk::Extent2D extent = swapchain_.extent();
    // 투영과 뷰는 모든 물체가 같으므로 한 번만 곱해 둔다. 물체별 model은 기록할 때 곱한다.
    const glm::mat4 viewProj = makeProjection(extent) * view;
    recordCommands(frame.commandBuffer, swapchain_.image(*imageIndex), swapchain_.depth(), extent,
                   frame.descriptorSet, items, viewProj);

    // 5. 큐에 제출한다. imageAvailable을 기다렸다가 실행하고, 끝나면 renderFinished를 신호한다.
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

    // 6. 화면 출력을 요청한다. renderFinished가 신호된 뒤에 출력된다.
    swapchain_.present(*imageIndex);

    frame_ = (frame_ + 1) % kMaxFramesInFlight;
}

void Renderer::recordCommands(const vk::raii::CommandBuffer& cmd, const SwapchainImage& target,
                              const DepthBuffer& depth, vk::Extent2D extent,
                              const vk::raii::DescriptorSet& descriptorSet, std::span<const DrawItem> items,
                              const glm::mat4& viewProj) {
    cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // 이미지 레이아웃 전환: 이전 내용은 버리고(Undefined) 색상 출력용으로.
    // srcStage를 이미지 획득 세마포어의 대기 단계와 맞춰, 이미지가 넘어온 뒤에 전환되게 한다.
    transitionImage(cmd, target.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput, {},
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);

    // 깊이 이미지도 레이아웃을 전환한다. 이전 내용은 버린다(Undefined). 어차피 아래에서 지운다.
    // 깊이 이미지는 프레임들이 같이 쓰므로, 앞 프레임의 깊이 테스트(읽기·쓰기)가 끝난 뒤에 이번 프레임이 시작되게 한다.
    // 배리어의 앞쪽 범위는 같은 큐에 먼저 제출된 명령까지 포함하므로 앞 프레임도 여기에 걸린다.
    constexpr auto kDepthStages =
        vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
    transitionImage(cmd, *depth.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    kDepthStages, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, kDepthStages,
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    kDepthRange);

    // 도형이 잘 보이도록 어두운 단색으로 지운다. (sRGB 포맷이므로 선형 값)
    const vk::ClearValue clear{.color = vk::ClearColorValue{.float32 = std::array<float, 4>{0.01f, 0.01f, 0.02f, 1.0f}}};
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = *target.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clear,
    };
    // 깊이: 가장 먼 값 1.0으로 지우고, 다 그린 뒤에는 저장하지 않는다. (화면에 나가는 것은 색상뿐)
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = *depth.view,
        .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = vk::ClearValue{.depthStencil = vk::ClearDepthStencilValue{.depth = 1.0f, .stencil = 0}},
    };
    // dynamic rendering: 렌더패스와 프레임버퍼 객체 없이 그 자리에서 대상을 지정한다.
    cmd.beginRendering(vk::RenderingInfo{
        .renderArea = vk::Rect2D{.offset = {0, 0}, .extent = extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    });

    // 파이프라인에서 동적으로 남겨둔 뷰포트와 시저를 지정한다. 이미지 전체에 그린다.
    // 모든 파이프라인이 이 둘을 동적 상태로 두었으므로, 파이프라인을 바꿔도 다시 지정할 필요가 없다.
    cmd.setViewport(0, vk::Viewport{
                           .x = 0.0f,
                           .y = 0.0f,
                           .width = static_cast<float>(extent.width),
                           .height = static_cast<float>(extent.height),
                           .minDepth = 0.0f,
                           .maxDepth = 1.0f,
                       });
    cmd.setScissor(0, vk::Rect2D{.offset = {0, 0}, .extent = extent});

    // 장면 공통 데이터(set 0)를 연결한다. 모든 파이프라인이 같은 레이아웃을 쓰므로
    // 파이프라인을 바꿔도 연결이 유지된다. (push constant도 마찬가지)
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout_, 0, *descriptorSet, nullptr);

    // 물체마다 파이프라인을 고르고, 행렬을 push constant로 바꿔 보내고 그린다.
    // push constant는 명령 버퍼에 값이 그대로 기록되므로, 다음 물체의 값으로 덮어써도 앞의 그리기에는 영향이 없다.
    const Pipeline* bound = nullptr;
    for (const DrawItem& item : items) {
        const Pipeline& pipeline = pipelineFor(item.mode);
        if (&pipeline != bound) {  // 같은 파이프라인이 이어지면 다시 바인딩하지 않는다.
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.handle());
            bound = &pipeline;
        }
        const PushConstants pushConstants{
            .mvp = viewProj * item.model,  // 오른쪽부터 적용: 모델 → 월드 → 카메라 → 클립
            .model = item.model,
        };
        cmd.pushConstants<PushConstants>(*pipelineLayout_, vk::ShaderStageFlagBits::eVertex, 0, pushConstants);
        item.mesh.record(cmd);  // 정점/인덱스 버퍼를 바인딩하고 그린다.
    }

    cmd.endRendering();

    // 이미지 레이아웃 전환: 색상 출력용에서 화면 출력용으로.
    // 화면 출력은 파이프라인 밖이므로 dstStage는 BottomOfPipe, 가시성은 renderFinished 세마포어가 보장한다.
    transitionImage(cmd, target.image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::PipelineStageFlagBits2::eBottomOfPipe, {});

    cmd.end();
}

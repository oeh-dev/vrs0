#include "Buffer.hpp"

#include <cstring>
#include <stdexcept>

#include "VulkanContext.hpp"

namespace {

// 디바이스의 메모리 타입 중에서 다음 두 조건을 만족하는 첫 번째 것을 고른다.
//  - typeBits: 이 버퍼가 쓸 수 있는 타입 (getMemoryRequirements가 알려준다)
//  - required: 필요한 속성 (예: CPU에서 쓸 수 있는가, GPU 전용으로 빠른가)
uint32_t findMemoryType(const vk::raii::PhysicalDevice& physicalDevice, uint32_t typeBits,
                        vk::MemoryPropertyFlags required) {
    const auto props = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        const bool allowed = typeBits & (1u << i);
        const bool matches = (props.memoryTypes[i].propertyFlags & required) == required;
        if (allowed && matches) return i;
    }
    throw std::runtime_error("No suitable memory type");
}

}  // namespace

GpuBuffer createBuffer(const VulkanContext& context, vk::DeviceSize size, vk::BufferUsageFlags usage,
                       vk::MemoryPropertyFlags properties) {
    const auto& device = context.device();

    // 버퍼 객체는 크기와 용도만 가진 껍데기다. 메모리는 따로 할당해 연결한다.
    vk::raii::Buffer buffer(device, vk::BufferCreateInfo{
                                        .size = size,
                                        .usage = usage,
                                        .sharingMode = vk::SharingMode::eExclusive,  // 큐 패밀리 하나만 사용
                                    });

    // 실제로 필요한 크기는 정렬 때문에 요청한 크기보다 클 수 있다.
    const auto req = buffer.getMemoryRequirements();
    vk::raii::DeviceMemory memory(
        device, vk::MemoryAllocateInfo{
                    .allocationSize = req.size,
                    .memoryTypeIndex = findMemoryType(context.physicalDevice(), req.memoryTypeBits, properties),
                });
    buffer.bindMemory(*memory, 0);

    return GpuBuffer{.memory = std::move(memory), .buffer = std::move(buffer), .size = size};
}

GpuBuffer createDeviceLocalBuffer(const VulkanContext& context, const void* data, vk::DeviceSize size,
                                  vk::BufferUsageFlags usage, vk::PipelineStageFlags2 dstStage,
                                  vk::AccessFlags2 dstAccess) {
    const auto& device = context.device();

    // 1. 스테이징 버퍼: CPU가 쓸 수 있는 메모리에 데이터를 복사한다.
    //    HOST_COHERENT이므로 flush 없이도 제출 시점에 GPU가 이 쓰기를 볼 수 있다.
    GpuBuffer staging = createBuffer(
        context, size, vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    void* mapped = staging.memory.mapMemory(0, size);
    std::memcpy(mapped, data, static_cast<size_t>(size));
    staging.memory.unmapMemory();

    // 2. 최종 버퍼: GPU 전용 메모리. 복사 대상이 되도록 eTransferDst를 더한다.
    //    (Apple Silicon처럼 메모리가 통합된 기기에서는 이득이 거의 없지만, 외장 GPU에서는 필요한 방식이다)
    GpuBuffer result = createBuffer(context, size, usage | vk::BufferUsageFlagBits::eTransferDst,
                                    vk::MemoryPropertyFlagBits::eDeviceLocal);

    // 3. 한 번 쓰고 버릴 커맨드 풀과 버퍼로 복사 명령을 기록한다.
    //    커맨드 버퍼를 풀보다 뒤에 선언해 풀보다 먼저 소멸하게 한다.
    const vk::raii::CommandPool pool(device, vk::CommandPoolCreateInfo{
                                                 .flags = vk::CommandPoolCreateFlagBits::eTransient,
                                                 .queueFamilyIndex = context.queueFamily(),
                                             });
    const auto commandBuffers = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
        .commandPool = *pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    });
    const vk::raii::CommandBuffer& cmd = commandBuffers.front();

    cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    cmd.copyBuffer(*staging.buffer, *result.buffer, vk::BufferCopy{.size = size});

    // 복사의 쓰기 결과가, 같은 큐에 뒤이어 제출되는 그리기의 정점/인덱스 읽기에 보이게 한다.
    // 배리어의 뒤쪽 범위는 같은 큐에서 나중에 제출되는 명령까지 포함한다.
    const vk::MemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
    };
    cmd.pipelineBarrier2(vk::DependencyInfo{
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    });
    cmd.end();

    const vk::CommandBufferSubmitInfo cmdInfo{.commandBuffer = *cmd};
    context.queue().submit2(vk::SubmitInfo2{
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
    });

    // 스테이징 버퍼와 커맨드 풀이 이 함수를 나가며 소멸하므로, 그 전에 GPU 작업이 끝나기를 기다린다.
    // (가시성은 위의 배리어가 맡는다. 여기서 기다리는 것은 객체를 안전하게 정리하기 위해서다)
    context.queue().waitIdle();

    return result;
}

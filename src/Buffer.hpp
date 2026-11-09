#pragma once

#include "vk.hpp"

class VulkanContext;

// 버퍼 하나와 그 메모리. 버퍼 하나에 할당 하나로 단순하게 둔다. (실서비스에서는 VMA 등으로 묶어 할당)
struct GpuBuffer {
    // 선언 순서 = 생성 순서, 소멸은 역순: 버퍼가 먼저, 메모리가 나중에 소멸한다.
    vk::raii::DeviceMemory memory = nullptr;
    vk::raii::Buffer buffer = nullptr;
    vk::DeviceSize size = 0;
};

// 버퍼를 만들고, properties를 만족하는 메모리를 할당해 연결한다.
GpuBuffer createBuffer(const VulkanContext& context, vk::DeviceSize size, vk::BufferUsageFlags usage,
                       vk::MemoryPropertyFlags properties);

// 호스트 데이터를 스테이징 버퍼를 거쳐 DEVICE_LOCAL 버퍼로 올린다. 복사가 끝날 때까지 기다린다.
// dstStage/dstAccess: 이 버퍼를 나중에 읽을 파이프라인 단계와 접근 종류.
GpuBuffer createDeviceLocalBuffer(const VulkanContext& context, const void* data, vk::DeviceSize size,
                                  vk::BufferUsageFlags usage, vk::PipelineStageFlags2 dstStage,
                                  vk::AccessFlags2 dstAccess);

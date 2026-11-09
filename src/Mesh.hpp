#pragma once

#include <cstdint>
#include <span>

#include "Buffer.hpp"
#include "Vertex.hpp"

class VulkanContext;

// 정점 버퍼와 인덱스 버퍼 한 쌍. 생성할 때 GPU로 올리고 이후에는 바뀌지 않는다.
class Mesh {
public:
    Mesh(const VulkanContext& context, std::span<const Vertex> vertices, std::span<const uint16_t> indices);

    // 버퍼를 바인딩하고 그리기 명령을 기록한다. 파이프라인은 미리 바인딩되어 있어야 한다.
    void record(const vk::raii::CommandBuffer& cmd) const;

private:
    GpuBuffer vertexBuffer_;
    GpuBuffer indexBuffer_;
    uint32_t indexCount_;
};

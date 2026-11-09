#include "Mesh.hpp"

Mesh::Mesh(const VulkanContext& context, std::span<const Vertex> vertices, std::span<const uint16_t> indices)
    : vertexBuffer_(createDeviceLocalBuffer(context, vertices.data(), vertices.size_bytes(),
                                            vk::BufferUsageFlagBits::eVertexBuffer,
                                            vk::PipelineStageFlagBits2::eVertexAttributeInput,
                                            vk::AccessFlagBits2::eVertexAttributeRead)),
      indexBuffer_(createDeviceLocalBuffer(context, indices.data(), indices.size_bytes(),
                                           vk::BufferUsageFlagBits::eIndexBuffer,
                                           vk::PipelineStageFlagBits2::eIndexInput,
                                           vk::AccessFlagBits2::eIndexRead)),
      indexCount_(static_cast<uint32_t>(indices.size())) {}

void Mesh::record(const vk::raii::CommandBuffer& cmd) const {
    const vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, *vertexBuffer_.buffer, offset);  // 0번 바인딩 (Vertex::binding)
    cmd.bindIndexBuffer(*indexBuffer_.buffer, 0, vk::IndexType::eUint16);

    // 인덱스 indexCount_개, 인스턴스 1개. 인덱스가 가리키는 정점마다 버텍스 셰이더가 실행된다.
    cmd.drawIndexed(indexCount_, 1, 0, 0, 0);
}

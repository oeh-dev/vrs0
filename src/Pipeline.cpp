#include "Pipeline.hpp"

#include <cstdint>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "Vertex.hpp"

namespace {

// SPIR-V 파일은 32비트 워드의 배열이다.
std::vector<uint32_t> readSpirv(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open shader: " + path);

    const std::streamsize size = file.tellg();
    if (size <= 0 || size % sizeof(uint32_t) != 0) throw std::runtime_error("Invalid SPIR-V file: " + path);

    std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), size);
    return code;
}

vk::raii::ShaderModule createShaderModule(const vk::raii::Device& device, const std::string& path) {
    const std::vector<uint32_t> code = readSpirv(path);
    return vk::raii::ShaderModule(device, vk::ShaderModuleCreateInfo{
                                              .codeSize = code.size() * sizeof(uint32_t),  // 바이트 단위
                                              .pCode = code.data(),
                                          });
}

}  // namespace

Pipeline::Pipeline(const vk::raii::Device& device, vk::Format colorFormat, const std::string& vertexShaderPath,
                   const std::string& fragmentShaderPath) {
    // 셰이더 모듈은 파이프라인 생성에만 필요하다. 생성이 끝나면 이 함수를 나가면서 소멸해도 된다.
    const vk::raii::ShaderModule vertexShader = createShaderModule(device, vertexShaderPath);
    const vk::raii::ShaderModule fragmentShader = createShaderModule(device, fragmentShaderPath);

    const std::array<vk::PipelineShaderStageCreateInfo, 2> stages{{
        {.stage = vk::ShaderStageFlagBits::eVertex, .module = *vertexShader, .pName = "main"},
        {.stage = vk::ShaderStageFlagBits::eFragment, .module = *fragmentShader, .pName = "main"},
    }};

    // 정점 입력: 버퍼를 어떤 간격으로 읽고(바인딩), 각 값을 셰이더의 어느 location에 넣을지(속성).
    // 가리키는 배열은 파이프라인 생성 시점까지 살아 있어야 하므로 지역 변수로 둔다.
    const auto binding = Vertex::binding();
    const auto attributes = Vertex::attributes();
    const vk::PipelineVertexInputStateCreateInfo vertexInput{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };

    // 정점 세 개씩 삼각형 하나로 묶는다.
    const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };

    // 뷰포트와 시저 영역은 그릴 때 지정한다. (아래 동적 상태) 여기서는 개수만 알려준다.
    const vk::PipelineViewportStateCreateInfo viewport{
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const vk::PipelineRasterizationStateCreateInfo rasterization{
        .polygonMode = vk::PolygonMode::eFill,
        // 뒷면 제거: 화면에서 시계 방향으로 보이는 삼각형은 그리지 않는다.
        // 모델은 바깥에서 볼 때 반시계 방향으로 정의하고, 투영 행렬의 y 반전으로 화면에서도 반시계가 유지된다.
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f,  // 기본값 0은 잘못된 값이다. 반드시 1.0으로 지정한다.
    };

    const vk::PipelineMultisampleStateCreateInfo multisample{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,  // 안티에일리어싱 없음
    };

    // 색상 출력 대상 하나. 블렌딩 없이 RGBA를 모두 쓴다.
    const vk::PipelineColorBlendAttachmentState blendAttachment{
        .blendEnable = VK_FALSE,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo colorBlend{
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
    };

    // 동적 상태: 파이프라인에 굳혀두지 않고 기록할 때 지정하는 값.
    // 뷰포트를 동적으로 두면 스왑체인 크기가 바뀌어도 파이프라인을 다시 만들 필요가 없다.
    const std::array<vk::DynamicState, 2> dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };

    // push constant는 버텍스 셰이더에서만 읽는다.
    const vk::PushConstantRange pushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    layout_ = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo{
                                                   .pushConstantRangeCount = 1,
                                                   .pPushConstantRanges = &pushConstantRange,
                                               });

    // dynamic rendering: 렌더패스 대신 그릴 대상의 포맷을 pNext로 알려준다.
    const vk::PipelineRenderingCreateInfo renderingInfo{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
    };

    const vk::GraphicsPipelineCreateInfo createInfo{
        .pNext = &renderingInfo,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = nullptr,  // 깊이 버퍼 없음. 5단계에서 추가한다.
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = *layout_,
    };
    pipeline_ = vk::raii::Pipeline(device, nullptr, createInfo);  // nullptr: 파이프라인 캐시 사용 안 함
}

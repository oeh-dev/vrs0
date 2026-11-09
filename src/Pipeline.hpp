#pragma once

#include <string>
#include <glm/glm.hpp>

#include "vk.hpp"

// 그릴 때마다 명령 버퍼에 직접 실어 보내는 작은 데이터. 셰이더의 push_constant 블록과 배치가 같아야 한다.
// 스펙이 보장하는 최소 크기는 128바이트이므로 그 안에 맞춘다.
struct PushConstants {
    glm::mat4 mvp;  // 모델 → 클립 공간. GLM과 GLSL 모두 열 우선(column-major)이라 그대로 복사하면 된다.
};
static_assert(sizeof(PushConstants) <= 128);

// 그래픽스 파이프라인 하나와 그 레이아웃을 소유한다.
//
// 파이프라인은 셰이더와 고정 기능 설정(정점 입력, 래스터화, 블렌딩 등)을 미리 한 객체로 묶은 것이다.
// 그릴 때는 bindPipeline 한 번으로 이 상태 전체가 적용된다.
class Pipeline {
public:
    // colorFormat: 그릴 대상 이미지의 포맷 (dynamic rendering에서는 생성 시 알려줘야 한다)
    Pipeline(const vk::raii::Device& device, vk::Format colorFormat, const std::string& vertexShaderPath,
             const std::string& fragmentShaderPath);

    const vk::raii::Pipeline& handle() const { return pipeline_; }
    const vk::raii::PipelineLayout& layout() const { return layout_; }

private:
    // 레이아웃: 셰이더가 받는 외부 자원(디스크립터, push constant)의 구성. 지금은 push constant 하나.
    vk::raii::PipelineLayout layout_ = nullptr;
    vk::raii::Pipeline pipeline_ = nullptr;
};

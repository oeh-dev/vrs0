#pragma once

#include <cstdint>
#include <string>

#include "vk.hpp"

// 같은 셰이더로 만드는 파이프라인 변형들을 구분하는 설정
struct PipelineOptions {
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;  // eLine이면 삼각형의 모서리만 그린다. (와이어프레임)
    int32_t fragmentMode = 0;  // 프래그먼트 셰이더의 특수화 상수 kShadingMode 값 (ShaderInterface.hpp의 FragmentMode)
};

// 그래픽스 파이프라인 하나를 소유한다.
//
// 파이프라인은 셰이더와 고정 기능 설정(정점 입력, 래스터화, 블렌딩 등)을 미리 한 객체로 묶은 것이다.
// 그릴 때는 bindPipeline 한 번으로 이 상태 전체가 적용된다.
class Pipeline {
public:
    // layout: 셰이더가 받는 외부 자원(디스크립터, push constant)의 구성.
    //         여러 파이프라인이 같은 레이아웃을 공유하므로 밖에서 만들어 넘긴다. 이 객체보다 오래 살아야 한다.
    // colorFormat, depthFormat: 그릴 대상 이미지들의 포맷 (dynamic rendering에서는 생성 시 알려줘야 한다)
    Pipeline(const vk::raii::Device& device, const vk::raii::PipelineLayout& layout, vk::Format colorFormat,
             vk::Format depthFormat, const std::string& vertexShaderPath, const std::string& fragmentShaderPath,
             const PipelineOptions& options);

    const vk::raii::Pipeline& handle() const { return pipeline_; }

private:
    vk::raii::Pipeline pipeline_ = nullptr;
};

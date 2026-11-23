#pragma once

// C++과 셰이더가 함께 보는 데이터의 배치. 셰이더 쪽 선언(lit.vert, lit.frag)과 반드시 같아야 한다.

#include <cstdint>

#include <glm/glm.hpp>

// push constant: 물체마다 바뀌는 값. 명령 버퍼에 직접 기록된다. (lit.vert의 PushConstants)
// 스펙이 보장하는 최소 크기는 128바이트이고, 이 구조체가 그 한도를 꽉 채운다.
struct PushConstants {
    glm::mat4 mvp;    // 모델 → 클립 공간. GLM과 GLSL 모두 열 우선(column-major)이라 그대로 복사하면 된다.
    glm::mat4 model;  // 모델 → 월드 공간. 조명을 월드 공간에서 계산하려고 위치와 법선을 옮길 때 쓴다.
};
static_assert(sizeof(PushConstants) <= 128);

// uniform buffer: 프레임마다 한 번 바뀌고 모든 물체가 같이 보는 값. (lit.frag의 FrameUniforms)
// uniform 블록은 std140 배치 규칙을 따른다. vec3가 16바이트 경계에 놓이는 등 C++과 어긋나기 쉬우므로
// 모두 vec4로 두어 양쪽 배치가 저절로 같아지게 한다. 남는 w 자리에는 다른 값을 담는다.
struct FrameUniforms {
    glm::vec4 cameraPos;   // xyz: 월드 공간 카메라 위치 (반사광 계산용)
    glm::vec4 lightPos;    // xyz: 월드 공간 점광원 위치, w: 영향 범위
    glm::vec4 lightColor;  // rgb: 색 × 세기
};
static_assert(sizeof(FrameUniforms) == 48);

// 프래그먼트 셰이더의 특수화 상수 kShadingMode에 넣는 값 (lit.frag의 설명과 같아야 한다)
namespace FragmentMode {
constexpr int32_t kSmooth = 0;     // 정점 법선을 보간해 조명
constexpr int32_t kFlat = 1;       // 화면 공간 미분으로 구한 면 법선으로 조명
constexpr int32_t kUnlit = 2;      // 조명 없이 정점 색 그대로 (광원 표시용)
constexpr int32_t kWireframe = 3;  // 조명 없이 한 가지 선 색
}  // namespace FragmentMode

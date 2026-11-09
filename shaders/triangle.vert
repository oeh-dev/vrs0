#version 450

// 5단계: 정점 데이터를 정점 버퍼에서 받고, push constant의 행렬로 클립 공간으로 변환한다.
// location 번호는 Vertex::attributes()의 location과 짝지어진다.

layout(push_constant) uniform PushConstants {
    mat4 mvp;  // 모델 → 클립 공간 (C++의 PushConstants와 배치가 같아야 한다)
} pc;

layout(location = 0) in vec2 inPos;    // 모델 좌표. x는 오른쪽, y는 위쪽이 +. z는 0으로 둔다.
layout(location = 1) in vec3 inColor;  // 선형 값

layout(location = 0) out vec3 outColor;  // 프래그먼트 셰이더로 넘길 값. 삼각형 안에서 보간된다.

void main() {
    gl_Position = pc.mvp * vec4(inPos, 0.0, 1.0);
    outColor = inColor;
}

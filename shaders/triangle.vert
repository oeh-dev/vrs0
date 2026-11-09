#version 450

// 5단계: 정점 데이터를 정점 버퍼에서 받는다.
// location 번호는 Vertex::attributes()의 location과 짝지어진다.

layout(location = 0) in vec2 inPos;    // Vulkan 정규화 장치 좌표: x는 오른쪽, y는 아래쪽이 +, 범위 -1..1
layout(location = 1) in vec3 inColor;  // 선형 값

layout(location = 0) out vec3 outColor;  // 프래그먼트 셰이더로 넘길 값. 삼각형 안에서 보간된다.

void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
    outColor = inColor;
}

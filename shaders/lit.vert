#version 450

// 정점을 클립 공간으로 옮기고, 조명 계산에 쓸 월드 공간 위치·법선과 색을 넘긴다.
// location 번호는 Vertex::attributes()의 location과 짝지어진다.

layout(push_constant) uniform PushConstants {
    mat4 mvp;    // 모델 → 클립 공간
    mat4 model;  // 모델 → 월드 공간
} pc;            // C++의 PushConstants와 배치가 같아야 한다.

layout(location = 0) in vec3 inPos;     // 모델 좌표
layout(location = 1) in vec3 inColor;   // 선형 값
layout(location = 2) in vec3 inNormal;  // 모델 좌표의 단위 법선

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec3 outNormal;  // 월드 공간 법선. 보간되면 길이가 1보다 짧아지므로 프래그먼트에서 다시 정규화한다.
layout(location = 2) out vec3 outWorldPos;  // 월드 공간 위치. 점광원까지의 방향과 거리, 시선 방향 계산에 쓴다.

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    outColor = inColor;

    // 법선은 방향이므로 이동은 빼고 회전·크기 부분(왼쪽 위 3×3)만 적용한다.
    // 크기가 축마다 다르면(비균일 크기) 이 방법이 틀리고 역전치 행렬이 필요하다. 지금은 회전과 이동뿐이다.
    outNormal = mat3(pc.model) * inNormal;
    outWorldPos = vec3(pc.model * vec4(inPos, 1.0));
}

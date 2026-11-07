#version 450

// 4단계: 정점 버퍼 없이, 정점 번호(gl_VertexIndex)로 셰이더 안의 배열에서 위치와 색을 꺼낸다.
// 5단계에서 정점 데이터를 버퍼로 옮기면 이 배열은 사라진다.

// Vulkan의 정규화 장치 좌표: x는 오른쪽, y는 아래쪽이 +이고, 화면은 -1..1 범위다.
const vec2 kPositions[3] = vec2[](
    vec2( 0.0, -0.6),   // 위
    vec2( 0.6,  0.5),   // 오른쪽 아래
    vec2(-0.6,  0.5)    // 왼쪽 아래
);

// 선형 값이다. sRGB 스왑체인이 저장할 때 인코딩한다.
const vec3 kColors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

layout(location = 0) out vec3 outColor;  // 프래그먼트 셰이더로 넘길 값. 삼각형 안에서 보간된다.

void main() {
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
    outColor = kColors[gl_VertexIndex];
}

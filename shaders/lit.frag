#version 450

// 확산광(Lambert) 하나와 약한 주변광. 계산은 월드 공간에서 한다.

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 outColor;  // 0번 색상 출력 대상 (RenderingInfo의 첫 번째 색상 첨부)

// 표면에서 빛 쪽을 가리키는 방향. 왼쪽 위 앞에서 비추는 평행광(태양처럼 아주 먼 광원).
const vec3 kToLight = normalize(vec3(-0.4, 0.6, 0.7));
const float kAmbient = 0.08;  // 빛이 닿지 않는 면도 완전히 검지 않도록

void main() {
    const vec3 n = normalize(inNormal);
    // 법선과 빛 방향이 이루는 각의 코사인. 정면으로 받으면 1, 비스듬할수록 줄고, 등지면 0.
    const float diffuse = max(dot(n, kToLight), 0.0);
    outColor = vec4(inColor * (kAmbient + diffuse), 1.0);
}

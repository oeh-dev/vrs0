#version 450

layout(location = 0) in vec3 inColor;     // 버텍스 셰이더의 outColor (location 번호로 짝지어진다)
layout(location = 0) out vec4 outColor;   // 0번 색상 출력 대상 (RenderingInfo의 첫 번째 색상 첨부)

void main() {
    outColor = vec4(inColor, 1.0);
}

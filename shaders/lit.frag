#version 450

// 조명: 약한 주변광 + 약한 평행광(해) + 움직이는 점광원(횃불) 하나. 계산은 월드 공간에서 한다.
// 반사 모델은 Blinn-Phong: 확산광(Lambert)에 반사광(하이라이트)을 더한다.

// 특수화 상수: 파이프라인을 만들 때 값이 정해진다. (ShaderInterface.hpp의 FragmentMode와 같아야 한다)
//   0 smooth:    정점 법선을 보간해 쓴다.
//   1 flat:      화면 공간 미분으로 면 법선을 구해 쓴다. 메시와 상관없이 삼각형마다 밝기가 일정하다.
//   2 unlit:     조명 없이 정점 색을 그대로 쓴다. (광원 표시용)
//   3 wireframe: 조명 없이 한 가지 선 색을 쓴다.
layout(constant_id = 0) const int kShadingMode = 0;

// 프레임마다 한 번 바뀌고 모든 물체가 같이 보는 값 (C++의 FrameUniforms와 배치가 같아야 한다)
layout(set = 0, binding = 0) uniform FrameUniforms {
    vec4 cameraPos;   // xyz: 월드 공간 카메라 위치
    vec4 lightPos;    // xyz: 월드 공간 점광원 위치, w: 영향 범위
    vec4 lightColor;  // rgb: 색 × 세기
} frameData;

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;  // 0번 색상 출력 대상 (RenderingInfo의 첫 번째 색상 첨부)

const vec3 kToSun = normalize(vec3(-0.4, 0.6, 0.7));  // 표면에서 해 쪽을 가리키는 방향 (평행광)
const vec3 kSunColor = vec3(0.3);
const float kAmbient = 0.03;          // 빛이 닿지 않는 면도 완전히 검지 않도록
const float kShininess = 64.0;        // 클수록 하이라이트가 작고 날카롭다
const vec3 kSpecularColor = vec3(0.5);
const vec3 kWireColor = vec3(0.6);

// 빛 하나가 표면에 주는 색.
// n: 법선, v: 표면 → 눈, l: 표면 → 빛 (모두 단위 벡터), radiance: 표면에 도달한 빛의 색과 세기
vec3 shade(vec3 n, vec3 v, vec3 l, vec3 radiance) {
    // 확산광: 법선과 빛 방향이 이루는 각의 코사인. 정면으로 받으면 1, 비스듬할수록 줄고, 등지면 0.
    const float diffuse = max(dot(n, l), 0.0);
    // 반사광: 빛과 시선의 중간 방향(half vector)이 법선과 가까울수록 밝다.
    // 빛을 등진 면(diffuse == 0)에는 하이라이트가 생기지 않게 한다.
    const vec3 h = normalize(l + v);
    const float specular = diffuse > 0.0 ? pow(max(dot(n, h), 0.0), kShininess) : 0.0;
    return radiance * (inColor * diffuse + kSpecularColor * specular);
}

void main() {
    // kShadingMode는 상수이므로 아래 분기는 파이프라인마다 한쪽만 남는다.
    if (kShadingMode == 2) {
        outColor = vec4(inColor, 1.0);
        return;
    }
    if (kShadingMode == 3) {
        outColor = vec4(kWireColor, 1.0);
        return;
    }

    vec3 n;
    if (kShadingMode == 1) {
        // 이웃 픽셀 사이의 월드 위치 차이(dFdx: 오른쪽 픽셀, dFdy: 아래쪽 픽셀)는 둘 다 삼각형 평면 위의 벡터다.
        // 그 외적이 면 법선이다. Vulkan의 화면 y는 아래쪽이 +이므로 dFdy × dFdx 순서여야 바깥을 향한다.
        n = normalize(cross(dFdy(inWorldPos), dFdx(inWorldPos)));
    } else {
        n = normalize(inNormal);  // 보간되면 길이가 1보다 짧아지므로 다시 정규화한다.
    }
    const vec3 v = normalize(frameData.cameraPos.xyz - inWorldPos);

    vec3 color = inColor * kAmbient;
    color += shade(n, v, kToSun, kSunColor);

    // 점광원: 평행광과 달리 빛이 오는 방향과 거리가 픽셀마다 다르다.
    const vec3 toLight = frameData.lightPos.xyz - inWorldPos;
    const float dist = length(toLight);
    const float range = frameData.lightPos.w;
    // 세기는 거리 제곱에 반비례한다. (+1은 아주 가까울 때 무한대로 치솟지 않게)
    // 그대로 두면 끝없이 멀리까지 영향을 주므로, range에서 0이 되도록 부드럽게 잘라낸다.
    const float window = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    const float attenuation = window * window / (dist * dist + 1.0);
    color += shade(n, v, toLight / dist, frameData.lightColor.rgb * attenuation);

    outColor = vec4(color, 1.0);
}

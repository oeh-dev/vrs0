#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// 궤도 카메라: target을 중심으로 한 구면 위에서 target을 바라본다.
// 위치를 직접 들고 있지 않고 각도 두 개와 거리로 나타내므로, "돌기"와 "다가가기"가 서로 섞이지 않는다.
struct OrbitCamera {
    glm::vec3 target{0.0f};
    float yaw = 0.0f;       // y축 둘레 각도(라디안). 0이면 +z 쪽에서 target을 본다. 늘면 카메라가 +x 쪽으로 돈다.
    float pitch = 0.0f;     // 수평면에서 올라간 각도(라디안). +면 위에서 내려다본다.
    float distance = 3.0f;  // target까지의 거리

    // lookAt은 시선과 위쪽 벡터(+y)의 외적으로 카메라의 오른쪽을 정한다.
    // 시선이 +y나 -y와 평행해지면 외적이 0이 되어 방향을 정할 수 없으므로, 수직 직전에서 멈춘다.
    static constexpr float kMaxPitch = glm::radians(85.0f);
    static constexpr float kMinDistance = 1.5f;
    static constexpr float kMaxDistance = 10.0f;

    void orbit(float deltaYaw, float deltaPitch) {
        yaw += deltaYaw;
        pitch = std::clamp(pitch + deltaPitch, -kMaxPitch, kMaxPitch);
    }

    // factor < 1이면 다가가고, > 1이면 멀어진다.
    // 더하지 않고 곱하는 이유: 가까울 때는 조금씩, 멀 때는 크게 움직여 체감 속도가 거리와 상관없이 일정하다.
    void zoom(float factor) { distance = std::clamp(distance * factor, kMinDistance, kMaxDistance); }

    glm::vec3 position() const {
        const float horizontal = distance * std::cos(pitch);  // 수평면에 비친 거리
        return target + glm::vec3(horizontal * std::sin(yaw), distance * std::sin(pitch), horizontal * std::cos(yaw));
    }

    // 월드 → 카메라
    glm::mat4 view() const { return glm::lookAt(position(), target, glm::vec3(0.0f, 1.0f, 0.0f)); }
};

#pragma once

#include <array>
#include <cstddef>

#include <glm/glm.hpp>

#include "vk.hpp"

// 정점 하나. 버퍼 안에서 이 구조체가 빈틈없이 반복된다.
struct Vertex {
    glm::vec2 pos;    // 정규화 장치 좌표 (5-3에서 3차원으로 바꾼다)
    glm::vec3 color;  // 선형 값. sRGB 스왑체인이 저장할 때 인코딩한다.

    // 클래스 정의 안에서는 Vertex가 아직 불완전한 타입이라 sizeof/offsetof를
    // static constexpr 멤버의 초기값으로 쓸 수 없다. 그래서 함수로 둔다.

    // 바인딩: 버퍼 하나를 어떤 간격으로 읽는가. 정점마다 sizeof(Vertex)만큼 나아간다.
    static vk::VertexInputBindingDescription binding() {
        return {.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
    }

    // 속성: 정점 안의 각 값이 셰이더의 어느 location으로, 어떤 형식으로 들어가는가.
    static std::array<vk::VertexInputAttributeDescription, 2> attributes() {
        return {{
            {.location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, pos)},
            {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, color)},
        }};
    }
};

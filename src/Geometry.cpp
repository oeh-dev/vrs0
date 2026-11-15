#include "Geometry.hpp"

#include <array>
#include <cmath>

#include <glm/glm.hpp>

MeshData makeIcosahedron(float radius) {
    // 정이십면체의 꼭짓점은 서로 수직인 황금 직사각형(1 × φ) 세 장의 꼭짓점이다.
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;  // 황금비 φ
    const std::array<glm::vec3, 12> corners{{
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},  // xy 평면의 직사각형
        {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},  // yz 평면
        {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1},  // zx 평면
    }};

    MeshData mesh;
    for (const glm::vec3& c : corners) {
        const glm::vec3 dir = glm::normalize(c);
        // 색은 방향을 0..1 범위로 옮긴 것. 조명이 없어도 어느 쪽 면인지 구분된다.
        mesh.vertices.push_back(Vertex{.pos = dir * radius, .color = dir * 0.5f + 0.5f});
    }

    // 꼭짓점 0 주변 5개, 그 이웃 5개, 꼭짓점 3 주변 5개, 그 이웃 5개.
    mesh.indices = {
        0, 11, 5,  0, 5,  1, 0, 1, 7, 0, 7,  10, 0, 10, 11,  //
        1, 5,  9,  5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1,  8,   //
        3, 9,  4,  3, 4,  2, 3, 2, 6, 3, 6,  8,  3, 8,  9,   //
        4, 9,  5,  2, 4,  11, 6, 2, 10, 8, 6, 7,  9, 8,  1,   //
    };
    return mesh;
}

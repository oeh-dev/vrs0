#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "Vertex.hpp"

// GPU에 올리기 전의 도형 데이터. Vulkan과 무관한 순수 CPU 쪽 데이터다.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;  // 삼각형마다 3개. 바깥에서 볼 때 반시계 방향.
};

// 모든 도형은 원점이 중심이고, 꼭짓점이 반지름 radius인 구 위에 있다.

// 정이십면체. 정점 12개, 면 20개. 정점을 면끼리 공유하고, 법선은 꼭짓점 방향이다.
MeshData makeIcosahedron(float radius, const glm::vec3& color);

// 깎은 정이십면체(축구공). 정이십면체의 모서리를 3등분해 꼭짓점 12개를 깎아낸 도형.
// 오각형 12개(검정)와 육각형 20개(흰색). 면마다 정점을 따로 두고 법선은 면 법선이다.
// 그래서 면 경계에서 밝기가 끊기고 모서리가 보인다.
MeshData makeTruncatedIcosahedron(float radius);

// 구(icosphere). 정이십면체의 삼각형을 subdivisions번 4등분하고 정점을 구면으로 밀어낸다.
// 정점을 공유하고 법선은 구의 법선(중심에서 정점 방향)이라 면 경계 없이 매끄럽다.
// 정점 수는 10 × 4^n + 2개 (n=3이면 642개).
MeshData makeIcosphere(float radius, int subdivisions, const glm::vec3& color);

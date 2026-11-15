#pragma once

#include <cstdint>
#include <vector>

#include "Vertex.hpp"

// GPU에 올리기 전의 도형 데이터. Vulkan과 무관한 순수 CPU 쪽 데이터다.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;  // 삼각형마다 3개. 바깥에서 볼 때 반시계 방향.
};

// 원점이 중심이고 꼭짓점이 반지름 radius인 구 위에 있는 정이십면체. 정점 12개, 면 20개.
// 정점을 면끼리 공유하므로 색이 면 사이에서 이어진다.
MeshData makeIcosahedron(float radius);

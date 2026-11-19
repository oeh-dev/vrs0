#include "Geometry.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <utility>

namespace {

// ---------------------------------------------------------------------------
// 정이십면체: 세 도형의 출발점

// 꼭짓점 12개는 서로 수직으로 끼워진 황금 직사각형(1 × φ) 세 장의 꼭짓점이다.
const float kPhi = (1.0f + std::sqrt(5.0f)) / 2.0f;  // 황금비 φ
const std::array<glm::vec3, 12> kIcoCorners{{
    {-1, kPhi, 0}, {1, kPhi, 0}, {-1, -kPhi, 0}, {1, -kPhi, 0},  // xy 평면의 직사각형
    {0, -1, kPhi}, {0, 1, kPhi}, {0, -1, -kPhi}, {0, 1, -kPhi},  // yz 평면
    {kPhi, 0, -1}, {kPhi, 0, 1}, {-kPhi, 0, -1}, {-kPhi, 0, 1},  // zx 평면
}};

// 면 20개. 바깥에서 볼 때 반시계 방향.
// 꼭짓점 0 주변 5개, 그 이웃 5개, 꼭짓점 3 주변 5개, 그 이웃 5개.
constexpr std::array<std::array<uint16_t, 3>, 20> kIcoFaces{{
    {0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},  //
    {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},    //
    {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},    //
    {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1},    //
}};

uint16_t toIndex(size_t i) {
    if (i > std::numeric_limits<uint16_t>::max()) throw std::runtime_error("Too many vertices for 16-bit indices");
    return static_cast<uint16_t>(i);
}

// 볼록 다각형 하나를 정점째로 추가한다. 정점은 바깥에서 볼 때 반시계 순서여야 한다.
// 면 법선은 다각형의 첫 세 점으로 계산하고, 이 면의 모든 정점에 같은 법선을 준다.
// 다각형은 첫 정점을 중심으로 부채꼴(0-1-2, 0-2-3, ...)로 삼각형을 나눈다.
void addFlatPolygon(MeshData& mesh, std::span<const glm::vec3> points, const glm::vec3& color) {
    const glm::vec3 normal = glm::normalize(glm::cross(points[1] - points[0], points[2] - points[0]));
    const uint16_t base = toIndex(mesh.vertices.size());
    for (const glm::vec3& p : points) {
        mesh.vertices.push_back(Vertex{.pos = p, .color = color, .normal = normal});
    }
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        mesh.indices.insert(mesh.indices.end(), {base, toIndex(base + i), toIndex(base + i + 1)});
    }
}

}  // namespace

MeshData makeIcosahedron(float radius, const glm::vec3& color) {
    MeshData mesh;
    for (const glm::vec3& c : kIcoCorners) {
        const glm::vec3 dir = glm::normalize(c);
        mesh.vertices.push_back(Vertex{.pos = dir * radius, .color = color, .normal = dir});
    }
    for (const auto& f : kIcoFaces) mesh.indices.insert(mesh.indices.end(), f.begin(), f.end());
    return mesh;
}

MeshData makeTruncatedIcosahedron(float radius) {
    // 모서리를 3등분하는 점. a 쪽에서 1/3 지점.
    auto third = [](const glm::vec3& a, const glm::vec3& b) { return a + (b - a) / 3.0f; };

    // 새 꼭짓점은 모두 같은 구 위에 있다. 그 반지름으로 나눠 radius에 맞춘다.
    const float scale = radius / glm::length(third(kIcoCorners[0], kIcoCorners[1]));
    const glm::vec3 black(0.02f);  // 선형 값. 완전한 0이면 조명 차이가 전혀 안 보인다.
    const glm::vec3 white(0.8f);

    MeshData mesh;

    // 오각형 12개: 꼭짓점 v를 깎은 자리. v에서 이웃 5개 쪽으로 1/3 지점들.
    // 이웃을 반시계 순서로 늘어놓아야 한다. v를 포함한 면 (v, x, y)는 반시계이므로
    // v 둘레를 반시계로 돌 때 x 다음에 y가 온다. 이 관계를 이어 붙이면 순서가 나온다.
    for (uint16_t v = 0; v < kIcoCorners.size(); ++v) {
        std::map<uint16_t, uint16_t> next;
        for (const auto& f : kIcoFaces) {
            for (int k = 0; k < 3; ++k) {
                if (f[k] == v) next[f[(k + 1) % 3]] = f[(k + 2) % 3];
            }
        }
        std::array<glm::vec3, 5> pentagon;
        uint16_t n = next.begin()->first;
        for (glm::vec3& p : pentagon) {
            p = third(kIcoCorners[v], kIcoCorners[n]) * scale;
            n = next[n];
        }
        addFlatPolygon(mesh, pentagon, black);
    }

    // 육각형 20개: 정이십면체의 면 (a, b, c)에서 세 꼭짓점을 잘라낸 나머지.
    // 각 변의 1/3, 2/3 지점을 면의 감는 방향대로 이으면 반시계 육각형이 된다.
    for (const auto& [a, b, c] : kIcoFaces) {
        const glm::vec3 &A = kIcoCorners[a], &B = kIcoCorners[b], &C = kIcoCorners[c];
        const std::array<glm::vec3, 6> hexagon{
            third(A, B) * scale, third(B, A) * scale,  //
            third(B, C) * scale, third(C, B) * scale,  //
            third(C, A) * scale, third(A, C) * scale,
        };
        addFlatPolygon(mesh, hexagon, white);
    }
    return mesh;  // 정점 12×5 + 20×6 = 180개, 삼각형 12×3 + 20×4 = 116개
}

MeshData makeIcosphere(float radius, int subdivisions, const glm::vec3& color) {
    // 단위 구 위의 정이십면체에서 시작한다. 위치(단위 벡터)가 곧 법선이다.
    std::vector<glm::vec3> dirs;
    for (const glm::vec3& c : kIcoCorners) dirs.push_back(glm::normalize(c));
    std::vector<std::array<uint16_t, 3>> faces(kIcoFaces.begin(), kIcoFaces.end());

    for (int s = 0; s < subdivisions; ++s) {
        // 모서리 하나의 중점은 그 모서리를 공유하는 두 삼각형이 같이 써야 한다.
        // 꼭짓점 번호 쌍(작은 번호 먼저)으로 이미 만든 중점을 찾는다.
        std::map<std::pair<uint16_t, uint16_t>, uint16_t> midpoints;
        auto midpoint = [&](uint16_t a, uint16_t b) {
            const auto key = std::minmax(a, b);
            if (const auto it = midpoints.find(key); it != midpoints.end()) return it->second;
            dirs.push_back(glm::normalize(dirs[a] + dirs[b]));  // 중점을 구면으로 밀어낸다.
            const uint16_t m = toIndex(dirs.size() - 1);
            midpoints.emplace(key, m);
            return m;
        };

        // 삼각형 하나를 4개로: 모서리 중점 셋으로 가운데 삼각형과 귀퉁이 셋. 감는 방향은 그대로 유지된다.
        std::vector<std::array<uint16_t, 3>> refined;
        for (const auto& [a, b, c] : faces) {
            const uint16_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            refined.insert(refined.end(), {{a, ab, ca}, {b, bc, ab}, {c, ca, bc}, {ab, bc, ca}});
        }
        faces = std::move(refined);
    }

    MeshData mesh;
    for (const glm::vec3& d : dirs) mesh.vertices.push_back(Vertex{.pos = d * radius, .color = color, .normal = d});
    for (const auto& f : faces) mesh.indices.insert(mesh.indices.end(), f.begin(), f.end());
    return mesh;
}

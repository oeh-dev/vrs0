#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "vk.hpp"

// 앱 수명 스코프: 프로그램이 끝날 때까지 한 번 만들어 계속 쓰는 Vulkan 객체들
//
// 인스턴스 → 검증 메신저 → 창 표면 → 디바이스 선택 → 논리 디바이스와 큐
class VulkanContext {
public:
    // requestedDevice가 있으면 그 번호의 물리 디바이스를 쓴다. (명령줄 --device)
    VulkanContext(GLFWwindow* window, std::optional<size_t> requestedDevice);

    const vk::raii::PhysicalDevice& physicalDevice() const { return physicalDevice_; }
    const vk::raii::SurfaceKHR& surface() const { return surface_; }
    const vk::raii::Device& device() const { return device_; }
    const vk::raii::Queue& queue() const { return queue_; }
    uint32_t queueFamily() const { return queueFamily_; }

private:
    // 선언 순서 = 생성 순서, 소멸은 역순
    vk::raii::Context context_;  // 로더 진입점. 인스턴스보다 오래 살아야 한다.
    vk::raii::Instance instance_ = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger_ = nullptr;
    vk::raii::SurfaceKHR surface_ = nullptr;
    vk::raii::PhysicalDevice physicalDevice_ = nullptr;
    uint32_t queueFamily_ = 0;  // 디바이스에게 되물을 수 없으므로 직접 보관한다.
    vk::raii::Device device_ = nullptr;
    vk::raii::Queue queue_ = nullptr;  // 디바이스가 소유. 소멸 시 할 일이 없다.
};

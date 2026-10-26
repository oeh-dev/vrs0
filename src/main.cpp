// 1단계: 창 열기, Vulkan 인스턴스 생성, 물리 디바이스 정보 출력
 
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
 
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
 
// 구조체를 지정 초기화(designated initializer)로 쓰기 위한 설정 (C++20)
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>
 
namespace {
 
constexpr int kWidth = 1280;
constexpr int kHeight = 720;
 
// glfwInit/glfwTerminate를 RAII로 묶는다.
// main에서 가장 먼저 선언해 가장 나중에 소멸되게 한다.
struct GlfwSession {
    GlfwSession() {
        if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    }
    ~GlfwSession() { glfwTerminate(); }
    GlfwSession(const GlfwSession&) = delete;
    GlfwSession& operator=(const GlfwSession&) = delete;
};
 
bool hasInstanceExtension(const std::vector<vk::ExtensionProperties>& props, const char* name) {
    for (const auto& p : props) {
        if (std::strcmp(p.extensionName.data(), name) == 0) return true;
    }
    return false;
}
 
void printVersion(const char* label, uint32_t v) {
    std::cout << "  " << label << VK_API_VERSION_MAJOR(v) << '.'
              << VK_API_VERSION_MINOR(v) << '.' << VK_API_VERSION_PATCH(v) << '\n';
}
 
void printPhysicalDevice(const vk::raii::PhysicalDevice& pd) {
    const auto props = pd.getProperties();
    std::cout << "Device: " << props.deviceName.data() << '\n';
    printVersion("apiVersion: ", props.apiVersion);
 
    // driverName/driverInfo는 Vulkan 1.2 코어 구조체에서 조회한다.
    if (props.apiVersion >= VK_API_VERSION_1_2) {
        const auto chain =
            pd.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
        const auto& driver = chain.get<vk::PhysicalDeviceDriverProperties>();
        std::cout << "  driverName: " << driver.driverName.data() << '\n'
                  << "  driverInfo: " << driver.driverInfo.data() << '\n';
    }
}
 
void run() {
    GlfwSession glfw;
 
    if (!glfwVulkanSupported()) throw std::runtime_error("GLFW: Vulkan loader not found");
 
    // GLFW가 창 표면(surface)에 필요로 하는 인스턴스 확장
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);
 
    vk::raii::Context context;  // 로더 진입점. 인스턴스보다 오래 살아야 한다.
 
    // MoltenVK 같은 portability 구현은 이 확장과 플래그가 있어야 열거된다.
    // 확장이 없는 플랫폼(Windows, Linux)에서도 같은 코드가 돌도록 조건부로 켠다.
    vk::InstanceCreateFlags instanceFlags{};
    // if (hasInstanceExtension(context.enumerateInstanceExtensionProperties(),
    //                          VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    //     extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    //     instanceFlags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    // }
 
    const vk::ApplicationInfo appInfo{
        .pApplicationName = "Vulkan RAII Sample",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = "None",
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = vk::ApiVersion13,
    };
 
    const vk::InstanceCreateInfo createInfo{
        .flags = instanceFlags,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
 
    vk::raii::Instance instance(context, createInfo);
 
    std::cout << "Instance extensions:\n";
    for (const char* e : extensions) std::cout << "  " << e << '\n';
 
    vk::raii::PhysicalDevices physicalDevices(instance);
    if (physicalDevices.empty()) throw std::runtime_error("No Vulkan physical device found");
    for (const auto& pd : physicalDevices) printPhysicalDevice(pd);
 
    // 아직 Vulkan으로 아무것도 그리지 않는 빈 창
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(kWidth, kHeight, "Vulkan RAII Sample", nullptr, nullptr);
    if (!window) throw std::runtime_error("glfwCreateWindow failed");
 
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }
    glfwDestroyWindow(window);
    // 여기서 instance → context → glfw 순으로 소멸된다.
}
 
}  // namespace
 
int main() {
    try {
        run();
    } catch (const vk::SystemError& e) {
        std::cerr << "Vulkan error: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

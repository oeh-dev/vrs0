// 2단계: 검증 레이어, 창 표면(surface), 디바이스 선택, 논리 디바이스와 큐 생성
 
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
 
// 구조체를 지정 초기화(designated initializer)로 쓰기 위한 설정 (C++20)
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>
 
// Vulkan 헤더를 먼저 포함해야 GLFW가 glfwCreateWindowSurface를 선언한다.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
 
namespace {
 
constexpr int kWidth = 1280;
constexpr int kHeight = 720;
 
#ifdef NDEBUG
constexpr bool kEnableValidation = false;
#else
constexpr bool kEnableValidation = true;
#endif
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
 
// provisional 확장이라 vulkan_beta.h의 매크로 대신 문자열을 직접 쓴다.
constexpr const char* kPortabilitySubsetExtension = "VK_KHR_portability_subset";
 
// glfwInit/glfwTerminate를 RAII로 묶는다.
struct GlfwSession {
    GlfwSession() {
        if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    }
    ~GlfwSession() { glfwTerminate(); }
    GlfwSession(const GlfwSession&) = delete;
    GlfwSession& operator=(const GlfwSession&) = delete;
};
 
struct WindowDeleter {
    void operator()(GLFWwindow* w) const { glfwDestroyWindow(w); }
};
using WindowPtr = std::unique_ptr<GLFWwindow, WindowDeleter>;
 
// ---------------------------------------------------------------------------
// 검증 레이어 메시지 출력
// C 타입 시그니처로 작성한다. (아래 createDebugMessenger의 주석 참고)
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void* /*userData*/) {
    const char* tag = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" : "WARNING";
    std::cerr << "[validation " << tag << "] " << data->pMessage << "\n\n";
    return VK_FALSE;  // 호출을 중단하지 않는다
}
 
vk::raii::DebugUtilsMessengerEXT createDebugMessenger(const vk::raii::Instance& instance) {
    using Severity = vk::DebugUtilsMessageSeverityFlagBitsEXT;
    using Type = vk::DebugUtilsMessageTypeFlagBitsEXT;
    const vk::DebugUtilsMessengerCreateInfoEXT info{
        .messageSeverity = Severity::eWarning | Severity::eError,
        .messageType = Type::eGeneral | Type::eValidation | Type::ePerformance,
        // Vulkan-Hpp 버전에 따라 이 멤버의 함수 포인터 타입이 C 타입 또는 C++ 타입이다.
        // 두 시그니처는 ABI가 같으므로, 어느 버전에서도 컴파일되도록 멤버 타입으로 캐스트한다.
        .pfnUserCallback = reinterpret_cast<decltype(vk::DebugUtilsMessengerCreateInfoEXT::pfnUserCallback)>(
            &debugCallback),
    };
    return vk::raii::DebugUtilsMessengerEXT(instance, info);
}
 
// ---------------------------------------------------------------------------
// 인스턴스
 
bool hasExtension(const std::vector<vk::ExtensionProperties>& props, const char* name) {
    for (const auto& p : props) {
        if (std::strcmp(p.extensionName.data(), name) == 0) return true;
    }
    return false;
}
 
bool hasLayer(const std::vector<vk::LayerProperties>& props, const char* name) {
    for (const auto& p : props) {
        if (std::strcmp(p.layerName.data(), name) == 0) return true;
    }
    return false;
}
 
vk::raii::Instance createInstance(const vk::raii::Context& context, bool& validationEnabled) {
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);
    std::vector<const char*> layers;
 
    vk::InstanceCreateFlags flags{};
    if (hasExtension(context.enumerateInstanceExtensionProperties(),
                     VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    }
 
    validationEnabled = false;
    if (kEnableValidation) {
        if (hasLayer(context.enumerateInstanceLayerProperties(), kValidationLayer)) {
            layers.push_back(kValidationLayer);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            validationEnabled = true;
        } else {
            std::cerr << "Warning: " << kValidationLayer << " not available, continuing without it\n";
        }
    }
 
    const vk::ApplicationInfo appInfo{
        .pApplicationName = "Vulkan RAII Sample",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = "None",
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = vk::ApiVersion13,
    };
 
    const vk::InstanceCreateInfo createInfo{
        .flags = flags,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    return vk::raii::Instance(context, createInfo);
}
 
// ---------------------------------------------------------------------------
// 물리 디바이스 선택
 
struct DeviceCandidate {
    size_t index;
    uint32_t queueFamily;  // 그래픽스와 화면 출력(present)을 모두 지원하는 큐 패밀리
    bool portability;      // VK_KHR_portability_subset을 노출하는 portability 구현인가

};
 
std::optional<uint32_t> findQueueFamily(const vk::raii::PhysicalDevice& pd,
                                        const vk::raii::SurfaceKHR& surface) {
    const auto families = pd.getQueueFamilyProperties();
    for (uint32_t i = 0; i < families.size(); ++i) {
        if ((families[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
            pd.getSurfaceSupportKHR(i, *surface)) {
            return i;
        }
    }
    return std::nullopt;
}
 
std::optional<DeviceCandidate> evaluateDevice(size_t index, const vk::raii::PhysicalDevice& pd,
                                              const vk::raii::SurfaceKHR& surface) {
    const auto props = pd.getProperties();
    if (props.apiVersion < VK_API_VERSION_1_3) return std::nullopt;
    if (!hasExtension(pd.enumerateDeviceExtensionProperties(), VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        return std::nullopt;
    }
 
    const auto features = pd.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
    const auto& f13 = features.get<vk::PhysicalDeviceVulkan13Features>();
    if (!f13.dynamicRendering || !f13.synchronization2) return std::nullopt;
 
    const auto queueFamily = findQueueFamily(pd, surface);
    if (!queueFamily) return std::nullopt;
 
    const auto chain = pd.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
    const bool portability =
        hasExtension(pd.enumerateDeviceExtensionProperties(), kPortabilitySubsetExtension);

    return DeviceCandidate{index, *queueFamily, portability};
}

void printPhysicalDevice(size_t index, const vk::raii::PhysicalDevice& pd) {
    const auto props = pd.getProperties();
    const uint32_t v = props.apiVersion;
    std::cout << '[' << index << "] " << props.deviceName.data() << '\n'
              << "  apiVersion: " << VK_API_VERSION_MAJOR(v) << '.' << VK_API_VERSION_MINOR(v) << '.'
              << VK_API_VERSION_PATCH(v) << '\n';
 
    if (v >= VK_API_VERSION_1_2) {
        const auto chain =
            pd.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
        const auto& d = chain.get<vk::PhysicalDeviceDriverProperties>();
        const auto& cv = d.conformanceVersion;
        std::cout << "  driverName: " << d.driverName.data() << '\n'
                  << "  driverInfo: " << d.driverInfo.data() << '\n'
                  << "  conformanceVersion: " << int(cv.major) << '.' << int(cv.minor) << '.'
                  << int(cv.subminor) << '.' << int(cv.patch) << '\n'
                  << "  portability: " << hasExtension(pd.enumerateDeviceExtensionProperties(), kPortabilitySubsetExtension) << '\n';
    }
}
 
// requestedIndex가 있으면 그 디바이스를, 없으면 portability가 아닌 드라이버를 우선해 고른다.
DeviceCandidate selectDevice(const vk::raii::PhysicalDevices& devices, const vk::raii::SurfaceKHR& surface,
                             std::optional<size_t> requestedIndex) {
    std::optional<DeviceCandidate> best;
    for (size_t i = 0; i < devices.size(); ++i) {
        printPhysicalDevice(i, devices[i]);
        const auto c = evaluateDevice(i, devices[i], surface);
        if (!c) {
            std::cout << "  -> requirements not met\n";
            continue;
        }
        if (requestedIndex) {
            if (*requestedIndex == i) best = c;
        } else if (!best || (best->portability && !c->portability)) {
            best = c;
        }
    }
    if (!best) {
        throw std::runtime_error(requestedIndex ? "Requested device is not usable"
                                                : "No suitable Vulkan device found");
    }
    return *best;
}
 
// ---------------------------------------------------------------------------
// 논리 디바이스
 
vk::raii::Device createDevice(const vk::raii::PhysicalDevice& pd, uint32_t queueFamily) {
    const float priority = 1.0f;
    const vk::DeviceQueueCreateInfo queueInfo{
        .queueFamilyIndex = queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
 
    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // 스펙상 디바이스가 이 확장을 지원하면 반드시 켜야 한다. (MoltenVK)
    if (hasExtension(pd.enumerateDeviceExtensionProperties(), kPortabilitySubsetExtension)) {
        extensions.push_back(kPortabilitySubsetExtension);
    }
 
    // 1.3 코어 기능을 켠다. pNext로 구조체를 연결하는 방식.
    vk::PhysicalDeviceVulkan13Features features13{
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    const vk::PhysicalDeviceFeatures2 features2{.pNext = &features13};
 
    const vk::DeviceCreateInfo createInfo{
        .pNext = &features2,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    return vk::raii::Device(pd, createInfo);
}
 
// ---------------------------------------------------------------------------
 
std::optional<size_t> parseDeviceArg(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--device") return static_cast<size_t>(std::stoul(argv[i + 1]));
    }
    return std::nullopt;
}
 
void run(std::optional<size_t> requestedDevice) {
    GlfwSession glfw;
    if (!glfwVulkanSupported()) throw std::runtime_error("GLFW: Vulkan loader not found");
 
    vk::raii::Context context;
    bool validationEnabled = false;
    vk::raii::Instance instance = createInstance(context, validationEnabled);
 
    // RAII 객체는 nullptr로 "아직 없음"을 표현할 수 있다.
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    if (validationEnabled) debugMessenger = createDebugMessenger(instance);
    std::cout << "Validation layer: " << (validationEnabled ? "on" : "off") << '\n';
 
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    WindowPtr window(glfwCreateWindow(kWidth, kHeight, "Vulkan RAII Sample", nullptr, nullptr));
    if (!window) throw std::runtime_error("glfwCreateWindow failed");
 
    // 창 표면: GLFW가 C 핸들을 만들고, RAII 객체가 소유권을 넘겨받는다.
    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(*instance, window.get(), nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("glfwCreateWindowSurface failed");
    }
    vk::raii::SurfaceKHR surface(instance, rawSurface);
 
    vk::raii::PhysicalDevices physicalDevices(instance);
    const DeviceCandidate chosen = selectDevice(physicalDevices, surface, requestedDevice);
    const vk::raii::PhysicalDevice& physicalDevice = physicalDevices[chosen.index];
    std::cout << "Selected device [" << chosen.index << "], queue family " << chosen.queueFamily << '\n';
 
    vk::raii::Device device = createDevice(physicalDevice, chosen.queueFamily);
    vk::raii::Queue queue = device.getQueue(chosen.queueFamily, 0);
 
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        if (glfwGetKey(window.get(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window.get(), GLFW_TRUE);
        }
    }
    device.waitIdle();
    // 선언의 역순으로 소멸: device → surface → window → debugMessenger → instance → context → glfw
}
 
}  // namespace
 
int main(int argc, char** argv) {
    try {
        run(parseDeviceArg(argc, argv));
    } catch (const vk::SystemError& e) {
        std::cerr << "Vulkan error: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

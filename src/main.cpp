// 3단계: 스왑체인과 dynamic rendering으로 화면을 지우는 프레임 루프
 
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
 
// 구조체를 지정 초기화(designated initializer)로 쓰기 위한 설정 (C++20)
#define VULKAN_HPP_NO_CONSTRUCTORS
// 스왑체인 무효화(OutOfDate)를 예외 대신 반환값으로 받는다.
#define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
#include <vulkan/vulkan_raii.hpp>
 
// Vulkan 헤더를 먼저 포함해야 GLFW가 glfwCreateWindowSurface를 선언한다.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
 
namespace {
 
constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr uint32_t kMaxFramesInFlight = 2;  // CPU가 GPU보다 앞서 준비할 수 있는 프레임 수
 
#ifdef NDEBUG
constexpr bool kEnableValidation = false;
#else
constexpr bool kEnableValidation = true;
#endif
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
 
// provisional 확장이라 vulkan_beta.h의 매크로 대신 문자열을 직접 쓴다.
constexpr const char* kPortabilitySubsetExtension = "VK_KHR_portability_subset";
 
// ---------------------------------------------------------------------------
// GLFW 자원의 RAII 래퍼
 
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
 
// C 타입 시그니처로 작성한다. (createDebugMessenger의 주석 참고)
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
// 확장과 레이어 조회
 
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
 
// ---------------------------------------------------------------------------
// 인스턴스
 
vk::raii::Instance createInstance(const vk::raii::Context& context, bool& validationEnabled) {
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);
    std::vector<const char*> layers;
 
    // MoltenVK 같은 portability 구현도 열거되도록 한다. (확장이 없는 플랫폼에서는 생략)
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
 
    const auto extensions = pd.enumerateDeviceExtensionProperties();
    if (!hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) return std::nullopt;
 
    const auto features = pd.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
    const auto& f13 = features.get<vk::PhysicalDeviceVulkan13Features>();
    if (!f13.dynamicRendering || !f13.synchronization2) return std::nullopt;
 
    const auto queueFamily = findQueueFamily(pd, surface);
    if (!queueFamily) return std::nullopt;
 
    return DeviceCandidate{index, *queueFamily, hasExtension(extensions, kPortabilitySubsetExtension)};
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
                  << int(cv.subminor) << '.' << int(cv.patch) << '\n';
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
        } else if (!best || (!c->portability && best->portability)) {
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
// 스왑체인 설정 선택
 
vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if (f.format == vk::Format::eB8G8R8A8Srgb && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return f;
        }
    }
    return formats.front();
}
 
vk::CompositeAlphaFlagBitsKHR chooseCompositeAlpha(vk::CompositeAlphaFlagsKHR supported) {
    for (auto bit : {vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::CompositeAlphaFlagBitsKHR::eInherit,
                     vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                     vk::CompositeAlphaFlagBitsKHR::ePostMultiplied}) {
        if (supported & bit) return bit;
    }
    throw std::runtime_error("No supported composite alpha mode");
}
 
// 이미지 전체(색상, 밉 0, 레이어 0)를 가리키는 범위
constexpr vk::ImageSubresourceRange kColorRange{
    .aspectMask = vk::ImageAspectFlagBits::eColor,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
};
 
// ---------------------------------------------------------------------------
// 애플리케이션
//
// 멤버는 선언 순서대로 생성되고 역순으로 소멸한다.
// 자식 객체(스왑체인, 커맨드 풀 등)는 반드시 부모(디바이스)보다 뒤에 선언해야 한다.
 
class App {
public:
    explicit App(std::optional<size_t> requestedDevice) {
        if (!glfwVulkanSupported()) throw std::runtime_error("GLFW: Vulkan loader not found");
 
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        window_.reset(glfwCreateWindow(kWidth, kHeight, "Vulkan RAII Sample", nullptr, nullptr));
        if (!window_) throw std::runtime_error("glfwCreateWindow failed");
 
        bool validationEnabled = false;
        instance_ = createInstance(context_, validationEnabled);
        if (validationEnabled) debugMessenger_ = createDebugMessenger(instance_);
        std::cout << "Validation layer: " << (validationEnabled ? "on" : "off") << '\n';
 
        // 창 표면: GLFW가 C 핸들을 만들고, RAII 객체가 소유권을 넘겨받는다.
        VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(*instance_, window_.get(), nullptr, &rawSurface) != VK_SUCCESS) {
            throw std::runtime_error("glfwCreateWindowSurface failed");
        }
        surface_ = vk::raii::SurfaceKHR(instance_, rawSurface);
 
        vk::raii::PhysicalDevices devices(instance_);
        const DeviceCandidate chosen = selectDevice(devices, surface_, requestedDevice);
        physicalDevice_ = std::move(devices[chosen.index]);
        queueFamily_ = chosen.queueFamily;
        std::cout << "Selected device [" << chosen.index << "], queue family " << queueFamily_ << '\n';
 
        device_ = createDevice(physicalDevice_, queueFamily_);
        queue_ = device_.getQueue(queueFamily_, 0);
 
        createSwapchain();
        createCommandBuffers();
        createFrameSync();
    }
 
    void run() {
        while (!glfwWindowShouldClose(window_.get())) {
            glfwPollEvents();
            if (glfwGetKey(window_.get(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(window_.get(), GLFW_TRUE);
            }
            drawFrame();
        }
        // GPU가 아직 사용 중인 객체를 소멸시키지 않도록 모든 작업이 끝나기를 기다린다.
        device_.waitIdle();
    }
 
private:
    // --- 스왑체인 ----------------------------------------------------------
 
    void createSwapchain() {
        const auto caps = physicalDevice_.getSurfaceCapabilitiesKHR(*surface_);
        const auto surfaceFormat = chooseSurfaceFormat(physicalDevice_.getSurfaceFormatsKHR(*surface_));
 
        // currentExtent가 특수값(0xFFFFFFFF)이면 창의 픽셀 크기로 직접 정한다.
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            extent_ = caps.currentExtent;
        } else {
            int w = 0, h = 0;
            glfwGetFramebufferSize(window_.get(), &w, &h);
            extent_ = vk::Extent2D{
                .width = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width),
                .height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height),
            };
        }
 
        // 최소 개수보다 하나 더 요청해 드라이버를 기다리는 일을 줄인다. (최대값 0은 제한 없음)
        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);
 
        const vk::SwapchainCreateInfoKHR info{
            .surface = *surface_,
            .minImageCount = imageCount,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = extent_,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,  // 큐 패밀리 하나만 사용
            .preTransform = caps.currentTransform,
            .compositeAlpha = chooseCompositeAlpha(caps.supportedCompositeAlpha),
            .presentMode = vk::PresentModeKHR::eFifo,  // 수직 동기화. 모든 구현이 반드시 지원한다.
            .clipped = VK_TRUE,
            .oldSwapchain = *swapchain_,  // 재생성 시 이전 스왑체인 (처음에는 null)
        };
        // 새 스왑체인을 만든 뒤 대입하므로, 이전 스왑체인은 생성이 끝난 다음에 소멸한다.
        swapchain_ = vk::raii::SwapchainKHR(device_, info);
        format_ = surfaceFormat.format;
 
        // 이미지는 스왑체인이 소유한다. 이미지 뷰와 출력 완료 세마포어는 이미지마다 만든다.
        images_ = swapchain_.getImages();
        imageViews_.clear();
        renderFinished_.clear();
        for (vk::Image image : images_) {
            imageViews_.emplace_back(device_, vk::ImageViewCreateInfo{
                                                  .image = image,
                                                  .viewType = vk::ImageViewType::e2D,
                                                  .format = format_,
                                                  .subresourceRange = kColorRange,
                                              });
            renderFinished_.emplace_back(device_, vk::SemaphoreCreateInfo{});
        }
        std::cout << "Swapchain: " << extent_.width << 'x' << extent_.height << ", " << images_.size()
                  << " images, " << vk::to_string(format_) << '\n';
    }
 
    void recreateSwapchain() {
        // 창이 최소화되어 크기가 0이면 복원될 때까지 기다린다.
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_.get(), &w, &h);
        while ((w == 0 || h == 0) && !glfwWindowShouldClose(window_.get())) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_.get(), &w, &h);
        }
        device_.waitIdle();
        createSwapchain();
    }
 
    // --- 커맨드 버퍼와 동기화 객체 ----------------------------------------
 
    void createCommandBuffers() {
        commandPool_ = vk::raii::CommandPool(
            device_, vk::CommandPoolCreateInfo{
                         .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,  // 버퍼를 개별로 다시 기록
                         .queueFamilyIndex = queueFamily_,
                     });
        commandBuffers_ = device_.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
            .commandPool = *commandPool_,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = kMaxFramesInFlight,
        });
    }
 
    void createFrameSync() {
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            imageAvailable_.emplace_back(device_, vk::SemaphoreCreateInfo{});
            // 첫 프레임에서 기다리지 않도록 신호된 상태로 만든다.
            inFlight_.emplace_back(device_, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
        }
    }
 
    // --- 프레임 ------------------------------------------------------------
 
    void drawFrame() {
        const vk::raii::Fence& inFlight = inFlight_[frame_];
 
        // 1. 이 프레임 슬롯을 마지막으로 쓴 GPU 작업이 끝나기를 기다린다.
        (void)device_.waitForFences(*inFlight, VK_TRUE, std::numeric_limits<uint64_t>::max());
 
        // 2. 그릴 스왑체인 이미지를 받는다. 이미지가 준비되면 imageAvailable이 신호된다.
        const auto acquired =
            swapchain_.acquireNextImage(std::numeric_limits<uint64_t>::max(), *imageAvailable_[frame_]);
        if (acquired.result == vk::Result::eErrorOutOfDateKHR) {
            recreateSwapchain();
            return;  // 펜스를 리셋하지 않았으므로 다음 프레임에서 다시 기다려도 멈추지 않는다.
        }
        const uint32_t imageIndex = acquired.value;
        device_.resetFences(*inFlight);
 
        // 3. 명령을 기록한다.
        const vk::raii::CommandBuffer& cmd = commandBuffers_[frame_];
        cmd.reset();
        recordCommands(cmd, imageIndex);
 
        // 4. 큐에 제출한다. imageAvailable을 기다렸다가 실행하고, 끝나면 renderFinished를 신호한다.
        const vk::SemaphoreSubmitInfo waitInfo{
            .semaphore = *imageAvailable_[frame_],
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        };
        const vk::CommandBufferSubmitInfo cmdInfo{.commandBuffer = *cmd};
        const vk::SemaphoreSubmitInfo signalInfo{
            .semaphore = *renderFinished_[imageIndex],
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        };
        queue_.submit2(vk::SubmitInfo2{
                           .waitSemaphoreInfoCount = 1,
                           .pWaitSemaphoreInfos = &waitInfo,
                           .commandBufferInfoCount = 1,
                           .pCommandBufferInfos = &cmdInfo,
                           .signalSemaphoreInfoCount = 1,
                           .pSignalSemaphoreInfos = &signalInfo,
                       },
                       *inFlight);  // GPU 작업이 끝나면 펜스가 신호된다.
 
        // 5. 화면 출력을 요청한다. renderFinished가 신호된 뒤에 출력된다.
        const vk::Semaphore presentWait = *renderFinished_[imageIndex];
        const vk::SwapchainKHR swapchain = *swapchain_;
        const vk::Result presentResult = queue_.presentKHR(vk::PresentInfoKHR{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presentWait,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex,
        });
        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR) {
            recreateSwapchain();
        }
 
        frame_ = (frame_ + 1) % kMaxFramesInFlight;
    }
 
    void recordCommands(const vk::raii::CommandBuffer& cmd, uint32_t imageIndex) {
        cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
 
        // 이미지 레이아웃 전환: 이전 내용은 버리고(Undefined) 색상 출력용으로
        transitionImage(cmd, images_[imageIndex], vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::PipelineStageFlagBits2::eColorAttachmentOutput, {},
                        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                        vk::AccessFlagBits2::eColorAttachmentWrite);
 
        // 시간에 따라 천천히 바뀌는 색으로 지운다. 색이 변하면 프레임 루프가 돌고 있다는 뜻이다.
        const float t = static_cast<float>(glfwGetTime());
        const vk::ClearValue clear{.color = vk::ClearColorValue{.float32 = std::array<float, 4>{
                                                                    0.5f + 0.5f * std::sin(t),
                                                                    0.5f + 0.5f * std::sin(t + 2.1f),
                                                                    0.5f + 0.5f * std::sin(t + 4.2f),
                                                                    1.0f,
                                                                }}};
        const vk::RenderingAttachmentInfo colorAttachment{
            .imageView = *imageViews_[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clear,
        };
        // dynamic rendering: 렌더패스와 프레임버퍼 객체 없이 그 자리에서 대상을 지정한다.
        cmd.beginRendering(vk::RenderingInfo{
            .renderArea = vk::Rect2D{.offset = {0, 0}, .extent = extent_},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
        });
        // 4단계부터 여기에 그리기 명령이 들어간다.
        cmd.endRendering();
 
        // 이미지 레이아웃 전환: 색상 출력용에서 화면 출력용으로
        transitionImage(cmd, images_[imageIndex], vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::ePresentSrcKHR, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eBottomOfPipe,
                        {});
 
        cmd.end();
    }
 
    // synchronization2 방식의 이미지 배리어: 레이아웃을 바꾸고, 앞뒤 작업의 순서와 메모리 가시성을 보장한다.
    static void transitionImage(const vk::raii::CommandBuffer& cmd, vk::Image image, vk::ImageLayout oldLayout,
                                vk::ImageLayout newLayout, vk::PipelineStageFlags2 srcStage,
                                vk::AccessFlags2 srcAccess, vk::PipelineStageFlags2 dstStage,
                                vk::AccessFlags2 dstAccess) {
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = srcStage,
            .srcAccessMask = srcAccess,
            .dstStageMask = dstStage,
            .dstAccessMask = dstAccess,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,  // 큐 패밀리 간 소유권 이전 없음
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = kColorRange,
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        });
    }
 
    // --- 멤버 (선언 순서 = 생성 순서, 소멸은 역순) --------------------------
 
    GlfwSession glfw_;
    WindowPtr window_;
 
    vk::raii::Context context_;
    vk::raii::Instance instance_ = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger_ = nullptr;
    vk::raii::SurfaceKHR surface_ = nullptr;
    vk::raii::PhysicalDevice physicalDevice_ = nullptr;
    uint32_t queueFamily_ = 0;  // 디바이스에게 되물을 수 없으므로 직접 보관한다.
 
    vk::raii::Device device_ = nullptr;
    vk::raii::Queue queue_ = nullptr;  // 디바이스가 소유. 소멸 시 할 일이 없다.
 
    // 스왑체인과 이미지마다 필요한 객체
    vk::raii::SwapchainKHR swapchain_ = nullptr;
    vk::Format format_ = vk::Format::eUndefined;
    vk::Extent2D extent_;
    std::vector<vk::Image> images_;  // 스왑체인이 소유
    std::vector<vk::raii::ImageView> imageViews_;
    std::vector<vk::raii::Semaphore> renderFinished_;  // 이미지마다: 그리기 완료 → 화면 출력
 
    // 프레임 슬롯마다 필요한 객체
    vk::raii::CommandPool commandPool_ = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers_;
    std::vector<vk::raii::Semaphore> imageAvailable_;  // 이미지 획득 → 그리기
    std::vector<vk::raii::Fence> inFlight_;            // GPU 작업 완료 → CPU
    uint32_t frame_ = 0;
};
 
std::optional<size_t> parseDeviceArg(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--device") return static_cast<size_t>(std::stoul(argv[i + 1]));
    }
    return std::nullopt;
}
 
}  // namespace
 
int main(int argc, char** argv) {
    try {
        App app(parseDeviceArg(argc, argv));
        app.run();
    } catch (const vk::SystemError& e) {
        std::cerr << "Vulkan error: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

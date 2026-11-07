cmake_minimum_required(VERSION 3.24)
project(VulkanRaiiSample LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # clangd용

# Vulkan SDK (헤더, 로더, vulkan_raii.hpp)와 셰이더 컴파일러 glslc
find_package(Vulkan REQUIRED COMPONENTS glslc)

# GLFW: 창과 입력
include(FetchContent)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

add_executable(vulkan_raii_sample
  src/main.cpp
  src/Window.cpp
  src/VulkanContext.cpp
  src/Swapchain.cpp
  src/Renderer.cpp
  src/Pipeline.cpp
)
target_link_libraries(vulkan_raii_sample PRIVATE Vulkan::Vulkan glfw)

# Vulkan-Hpp와 GLFW의 설정 매크로. 파일마다 달라지면 같은 구조체가 다르게 정의되므로
# 소스 코드가 아니라 여기 한곳에서 모든 파일에 똑같이 적용한다.
target_compile_definitions(vulkan_raii_sample PRIVATE
  VULKAN_HPP_NO_CONSTRUCTORS                     # 구조체를 지정 초기화로 작성 (C++20)
  VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS # 스왑체인 무효화를 예외 대신 반환값으로
  GLFW_INCLUDE_NONE                              # GLFW가 OpenGL 헤더를 포함하지 않게
)

# 한국어 Windows의 MSVC가 한글 주석을 CP949로 잘못 읽지 않도록
if(MSVC)
  target_compile_options(vulkan_raii_sample PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# 셰이더: GLSL 소스를 SPIR-V로 컴파일해 빌드 디렉터리의 shaders/에 둔다.
# 소스가 바뀌었을 때만 다시 컴파일하고, 실행 파일보다 먼저 만들어지게 한다.
set(SHADER_SOURCES
  shaders/triangle.vert
  shaders/triangle.frag
)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)

foreach(src ${SHADER_SOURCES})
  get_filename_component(name ${src} NAME)
  set(out ${SHADER_OUTPUT_DIR}/${name}.spv)
  add_custom_command(
    OUTPUT  ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND Vulkan::glslc --target-env=vulkan1.3 -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/${src}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src}
    COMMENT "Compiling shader ${name}"
  )
  list(APPEND SHADER_OUTPUTS ${out})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(vulkan_raii_sample shaders)

# 실행 시 셰이더를 읽을 위치. 샘플이므로 빌드 디렉터리의 절대 경로를 그대로 쓴다.
target_compile_definitions(vulkan_raii_sample PRIVATE SHADER_DIR="${SHADER_OUTPUT_DIR}")

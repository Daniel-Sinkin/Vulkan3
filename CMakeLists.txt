cmake_minimum_required(VERSION 3.20)
project(pba_vulkan_mvp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

find_package(Vulkan REQUIRED)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.3.9
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_lib SYSTEM PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw Vulkan::Vulkan)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.3
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
  vma
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG        v3.1.0
)
FetchContent_MakeAvailable(vma)

add_library(vma_hdr INTERFACE)
target_include_directories(vma_hdr SYSTEM INTERFACE
  ${vma_SOURCE_DIR}/include
)

file(GLOB_RECURSE PBA_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pba/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pba/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pba/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pba/*.cc
)

add_executable(main
  src/app/main.cpp
  ${PBA_SOURCES}
)

target_include_directories(main PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(main PRIVATE
  imgui_lib
  glfw
  Vulkan::Vulkan
  glm::glm
  vma_hdr
)

target_compile_options(main PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wshadow
  -Wconversion
  -Wfloat-conversion
  -Wsign-conversion
  -Wcast-qual
  -Wcast-align
  -Wformat=2
  -Wundef
  -Wnull-dereference
  -Wnon-virtual-dtor
  -Woverloaded-virtual
  -Wold-style-cast
  -Wzero-as-null-pointer-constant
  -Wdouble-promotion
  -Wimplicit-fallthrough
  -Wreturn-type
)

find_program(GLSLANG_VALIDATOR NAMES glslangValidator
  HINTS
    "$ENV{VULKAN_SDK}/Bin"
    "$ENV{VULKAN_SDK}/Bin32"
    "$ENV{VULKAN_SDK}/macOS/bin"
    "$ENV{VULKAN_SDK}/x86_64/bin"
)
if (NOT GLSLANG_VALIDATOR)
  message(FATAL_ERROR "glslangValidator not found. Install Vulkan SDK or provide glslangValidator in PATH.")
endif()

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders)
set(SHADER_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/assets/shaders)

set(SHADER_SOURCES
  ${SHADER_SRC_DIR}/cube.vert
  ${SHADER_SRC_DIR}/cube.frag
)

set(SHADER_SPV_OUT "")
foreach(SHADER_FILE IN LISTS SHADER_SOURCES)
  get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
  set(SHADER_SPV ${SHADER_BIN_DIR}/${SHADER_NAME}.spv)

  get_filename_component(SHADER_EXT ${SHADER_FILE} EXT)
  if (SHADER_EXT STREQUAL ".vert")
    set(SHADER_STAGE "vert")
  elseif (SHADER_EXT STREQUAL ".frag")
    set(SHADER_STAGE "frag")
  else()
    message(FATAL_ERROR "Unknown shader extension: ${SHADER_FILE}")
  endif()

  add_custom_command(
    OUTPUT ${SHADER_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
    COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.2 -S ${SHADER_STAGE} ${SHADER_FILE} -o ${SHADER_SPV}
    DEPENDS ${SHADER_FILE}
    COMMENT "Compiling ${SHADER_NAME} -> ${SHADER_NAME}.spv"
    VERBATIM
  )
  list(APPEND SHADER_SPV_OUT ${SHADER_SPV})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_SPV_OUT})
add_dependencies(main shaders)

add_custom_command(TARGET main POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/assets
          ${CMAKE_CURRENT_BINARY_DIR}/assets
)

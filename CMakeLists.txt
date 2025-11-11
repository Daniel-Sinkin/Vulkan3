# CMakeLists.txt
cmake_minimum_required(VERSION 3.23)
project(Vulkan3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

add_subdirectory(third_party/glm)
add_subdirectory(engine)

option(DSE_PORTABILITY_REQUIRED "Enable VK_KHR_portability_* on this platform" OFF)
if(APPLE)
  set(DSE_PORTABILITY_REQUIRED ON)
endif()


add_executable(DSEngine main.cpp)
target_link_libraries(DSEngine PRIVATE Engine SDL3::SDL3 Vulkan::Vulkan glm::glm)

foreach(tgt IN ITEMS Engine DSEngine ImGuiLib)
  target_compile_definitions(${tgt} PRIVATE
    $<$<BOOL:${DSE_PORTABILITY_REQUIRED}>:DSE_PORTABILITY_REQUIRED=1>
    $<$<CONFIG:Debug>:APP_USE_VULKAN_DEBUG_REPORT=1>
  )
endforeach()

set(CMAKE_MACOSX_RPATH ON)
set_target_properties(DSEngine PROPERTIES
  BUILD_RPATH "/opt/homebrew/lib"
  INSTALL_RPATH "@loader_path"
)
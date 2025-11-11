# engine/CMakeLists.txt
file(GLOB_RECURSE ENGINE_SRC CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SRC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
add_library(ImGuiLib STATIC ${IMGUI_SRC})
target_include_directories(ImGuiLib PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(ImGuiLib PUBLIC SDL3::SDL3 Vulkan::Vulkan)

add_library(Engine STATIC ${ENGINE_SRC})
target_include_directories(Engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(Engine
    PUBLIC  SDL3::SDL3 Vulkan::Vulkan glm::glm ImGuiLib
)

target_compile_definitions(Engine PRIVATE
    $<$<CONFIG:Debug>:APP_USE_VULKAN_DEBUG_REPORT>
)
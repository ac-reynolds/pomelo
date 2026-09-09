cmake_minimum_required(VERSION 3.24)
project(pomelo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

find_package(Vulkan REQUIRED)
find_program(POMELO_DXC_EXECUTABLE NAMES pomelo-dxc dxc
    HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin" "$ENV{HOME}/.local/bin"
    DOC "Host DXC executable with SPIR-V support" NO_CMAKE_FIND_ROOT_PATH REQUIRED)
execute_process(COMMAND "${POMELO_DXC_EXECUTABLE}" --version
    RESULT_VARIABLE dxc_result OUTPUT_VARIABLE dxc_version ERROR_VARIABLE dxc_error)
if(NOT dxc_result EQUAL 0)
    message(FATAL_ERROR "Cannot run DXC: ${dxc_error}. Set POMELO_DXC_EXECUTABLE to a native SPIR-V-enabled compiler.")
endif()
message(STATUS "DXC: ${POMELO_DXC_EXECUTABLE}: ${dxc_version}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dxc_probe.hlsl"
    "float4 main(float4 p : POSITION) : SV_Position { return p; }\n")
execute_process(COMMAND "${POMELO_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.2
    -HV 2021 -T vs_6_0 -E main -Fo "${CMAKE_CURRENT_BINARY_DIR}/dxc_probe.spv"
    "${CMAKE_CURRENT_BINARY_DIR}/dxc_probe.hlsl"
    RESULT_VARIABLE dxc_result OUTPUT_VARIABLE dxc_output ERROR_VARIABLE dxc_error)
if(NOT dxc_result EQUAL 0)
    message(FATAL_ERROR "DXC SPIR-V probe failed: ${dxc_output}${dxc_error}")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

FetchContent_Declare(
    SDL
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    GIT_SHALLOW TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
# Keep the starter buildable on minimal Linux installations. This optional
# integration is unrelated to window creation or Vulkan presentation.
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_core STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui_core PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

add_library(vulkan_backend STATIC
    src/graphics/vulkan_backend.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(vulkan_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vulkan_backend PRIVATE scene_renderer imgui_core SDL3::SDL3 Vulkan::Vulkan)

add_library(geometry STATIC src/geometry/uniform_grid.cpp)
target_include_directories(geometry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(scene_renderer STATIC src/graphics/scene_renderer.cpp)
target_include_directories(scene_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(scene_renderer PRIVATE geometry Vulkan::Vulkan)
foreach(stage vert frag)
    if(stage STREQUAL "vert")
        set(profile vs_6_0)
    else()
        set(profile ps_6_0)
    endif()
    set(spirv ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/cube_${stage}.spv)
    set(source ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/cube_${stage}.cpp)
    add_custom_command(OUTPUT ${spirv} ${source}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>
        COMMAND "${POMELO_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.2
                -HV 2021 -T ${profile} -E main $<IF:$<CONFIG:Debug>,-Od,-O3>
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cube.${stage}.hlsl -Fo ${spirv}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/deployment/embed_spirv.py
                ${spirv} ${source} --symbol cube_${stage}_spirv
                --header graphics/shaders.hpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cube.${stage}.hlsl
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cube_interface.hlsli
                "${POMELO_DXC_EXECUTABLE}"
                ${CMAKE_CURRENT_SOURCE_DIR}/deployment/embed_spirv.py
        VERBATIM
    )
    target_sources(scene_renderer PRIVATE ${source})
endforeach()

add_library(chrome STATIC
    src/chrome/chrome.cpp
    src/chrome/footer.cpp
    src/chrome/viewport.cpp
    src/chrome/simulation.cpp
    src/chrome/rendering.cpp
    src/chrome/panel.cpp
)
target_include_directories(chrome PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(chrome PRIVATE imgui_core)

add_library(application STATIC
    src/application/application.cpp
    src/application/window.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(application PUBLIC scene_renderer chrome vulkan_backend
    PRIVATE geometry imgui_core SDL3::SDL3)

add_executable(pomelo src/application/main.cpp)
target_link_libraries(pomelo PRIVATE application)

if(MSVC)
    target_compile_options(pomelo PRIVATE /W4 /permissive-)
else()
    target_compile_options(pomelo PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(uniform_grid_test tests/uniform_grid_test.cpp)
    target_link_libraries(uniform_grid_test PRIVATE geometry)
    add_test(NAME uniform_grid COMMAND uniform_grid_test)
endif()

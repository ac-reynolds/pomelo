cmake_minimum_required(VERSION 3.24)
project(pomelo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(POMELO_BUILD_PYTHON "Build the Python extension" OFF)
if(POMELO_BUILD_PYTHON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
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

# Project code is organized into application and graphics libraries.
add_library(graphics STATIC
    src/graphics/vulkan_backend.cpp
    src/graphics/volume_view.cpp
    src/graphics/render_target.cpp
    src/graphics/fields.cpp
    src/graphics/image.cpp
    src/graphics/projection.cpp
    src/graphics/pressure.cpp
    src/graphics/divergence.cpp
    src/graphics/smoke.cpp
    src/graphics/advection.cpp
    src/graphics/initial_conditions.cpp
    src/graphics/uniform_grid.cpp
)
target_include_directories(graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(graphics PUBLIC Vulkan::Vulkan PRIVATE SDL3::SDL3)

set(advect_spirv ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/advect_comp.spv)
set(advect_source ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/advect_comp.cpp)
add_custom_command(OUTPUT ${advect_spirv} ${advect_source}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>
    COMMAND "${POMELO_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.2
            -HV 2021 -T cs_6_0 -E main $<IF:$<CONFIG:Debug>,-Od,-O3>
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/advect.comp.hlsl -Fo ${advect_spirv}
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/deployment/embed_spirv.py
            ${advect_spirv} ${advect_source} --symbol advect_comp_spirv --header graphics/shaders.hpp
    DEPENDS shaders/advect.comp.hlsl shaders/volume_sampling.hlsli
            "${POMELO_DXC_EXECUTABLE}" deployment/embed_spirv.py
    VERBATIM
)
target_sources(graphics PRIVATE ${advect_source})
set(divergence_spirv ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/divergence_comp.spv)
set(divergence_source ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/divergence_comp.cpp)
add_custom_command(OUTPUT ${divergence_spirv} ${divergence_source}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>
    COMMAND "${POMELO_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.2
            -HV 2021 -T cs_6_0 -E main $<IF:$<CONFIG:Debug>,-Od,-O3>
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/divergence.comp.hlsl -Fo ${divergence_spirv}
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/deployment/embed_spirv.py
            ${divergence_spirv} ${divergence_source} --symbol divergence_comp_spirv --header graphics/shaders.hpp
    DEPENDS shaders/divergence.comp.hlsl shaders/volume_sampling.hlsli
            "${POMELO_DXC_EXECUTABLE}" deployment/embed_spirv.py
    VERBATIM
)
target_sources(graphics PRIVATE ${divergence_source})
foreach(shader smoke pressure pressure_reduce pressure_statistics pressure_normalize projection)
    set(spirv ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/${shader}_comp.spv)
    set(source ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/${shader}_comp.cpp)
    add_custom_command(OUTPUT ${spirv} ${source}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>
        COMMAND "${POMELO_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.2
                -HV 2021 -T cs_6_0 -E main $<IF:$<CONFIG:Debug>,-Od,-O3>
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}.comp.hlsl -Fo ${spirv}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/deployment/embed_spirv.py
                ${spirv} ${source} --symbol ${shader}_comp_spirv --header graphics/shaders.hpp
        DEPENDS shaders/${shader}.comp.hlsl shaders/pressure_interface.hlsli
                "${POMELO_DXC_EXECUTABLE}" deployment/embed_spirv.py
        VERBATIM
    )
    target_sources(graphics PRIVATE ${source})
endforeach()
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
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volume_sampling.hlsli
                "${POMELO_DXC_EXECUTABLE}"
                ${CMAKE_CURRENT_SOURCE_DIR}/deployment/embed_spirv.py
        VERBATIM
    )
    target_sources(graphics PRIVATE ${source})
endforeach()

add_library(application STATIC
    src/application/application.cpp
    src/application/window.cpp
    src/application/settings.cpp
    src/application/input_deck.cpp
)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(application PRIVATE graphics SDL3::SDL3)

add_executable(pomelo src/application/main.cpp)
target_link_libraries(pomelo PRIVATE application)

if(MSVC)
    target_compile_options(pomelo PRIVATE /W4 /permissive-)
else()
    target_compile_options(pomelo PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(input_deck_test tests/input_deck_test.cpp)
    target_link_libraries(input_deck_test PRIVATE application)
    add_test(NAME input_deck COMMAND input_deck_test ${CMAKE_CURRENT_SOURCE_DIR}/inputs)
    add_executable(uniform_grid_test tests/uniform_grid_test.cpp)
    target_link_libraries(uniform_grid_test PRIVATE graphics)
    add_test(NAME uniform_grid COMMAND uniform_grid_test)
    add_executable(advection_test tests/advection_test.cpp)
    target_link_libraries(advection_test PRIVATE graphics)
    add_test(NAME advection COMMAND advection_test)
    add_executable(pressure_test tests/pressure_test.cpp)
    target_link_libraries(pressure_test PRIVATE graphics)
    add_test(NAME pressure COMMAND pressure_test)
    add_executable(projection_test tests/projection_test.cpp)
    target_link_libraries(projection_test PRIVATE graphics)
    add_test(NAME projection COMMAND projection_test)
    set_tests_properties(advection pressure projection PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(demo_gpu_test tests/demo_gpu_test.cpp)
    target_link_libraries(demo_gpu_test PRIVATE application graphics)
    add_test(NAME demo_gpu COMMAND demo_gpu_test)
    set_tests_properties(demo_gpu PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(POMELO_BUILD_PYTHON)
    add_subdirectory(python)
endif()

# Optional reproducible image capture without a window or display server. This
# uses the application's input-deck settings, GPU simulation, and volume renderer.
option(POMELO_BUILD_CAPTURE "Build the surface-free Vulkan capture utility" OFF)
if(POMELO_BUILD_CAPTURE)
    add_executable(pomelo_capture tools/capture.cpp)
    target_link_libraries(pomelo_capture PRIVATE application graphics)
endif()

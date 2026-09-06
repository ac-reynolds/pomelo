cmake_minimum_required(VERSION 3.24)
project(pomelo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

find_package(Vulkan REQUIRED)

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

add_library(platform STATIC
    src/platform/platform.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
)
target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(platform PRIVATE imgui_core SDL3::SDL3)

add_library(vulkan_backend STATIC
    src/graphics/vulkan_backend.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(vulkan_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vulkan_backend PRIVATE imgui_core SDL3::SDL3 Vulkan::Vulkan)

add_library(scene_renderer STATIC src/graphics/scene_renderer.cpp)
target_include_directories(scene_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(scene_renderer PRIVATE vulkan_backend Vulkan::Vulkan)

add_library(chrome STATIC src/chrome/chrome.cpp)
target_include_directories(chrome PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(chrome PRIVATE imgui_core)

add_library(application STATIC src/application/application.cpp)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(application PUBLIC scene_renderer chrome platform vulkan_backend)

add_executable(pomelo src/application/main.cpp)
target_link_libraries(pomelo PRIVATE application)

if(MSVC)
    target_compile_options(pomelo PRIVATE /W4 /permissive-)
else()
    target_compile_options(pomelo PRIVATE -Wall -Wextra -Wpedantic)
endif()

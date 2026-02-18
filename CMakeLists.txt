cmake_minimum_required(VERSION 3.20)

project(Oxikara VERSION 0.5.0 LANGUAGES CXX)

option(OXIKARA_ENABLE_VULKAN "Enable Vulkan SDK discovery" ON)
option(OXIKARA_ENABLE_GLFW "Enable GLFW renderer path" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(oxikara
    src/main.cpp
    src/app.cpp
    src/audio/midi_output.cpp
    src/core/config.cpp
    src/core/log.cpp
    src/midi/stream_parser.cpp
    src/midi/song.cpp
    src/render/vulkan_renderer.cpp
)

target_include_directories(oxikara PRIVATE include)

if (MSVC)
    target_compile_options(oxikara PRIVATE /W4 /permissive-)
else()
    target_compile_options(oxikara PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (OXIKARA_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if (Vulkan_FOUND)
        target_link_libraries(oxikara PRIVATE Vulkan::Vulkan)
        target_compile_definitions(oxikara PRIVATE OXIKARA_HAS_VULKAN=1)
    endif()
endif()

if (OXIKARA_ENABLE_GLFW)
    find_package(glfw3 QUIET)
    if (TARGET glfw)
        target_link_libraries(oxikara PRIVATE glfw)
        target_compile_definitions(oxikara PRIVATE OXIKARA_HAS_GLFW=1)
    elseif (TARGET glfw3::glfw)
        target_link_libraries(oxikara PRIVATE glfw3::glfw)
        target_compile_definitions(oxikara PRIVATE OXIKARA_HAS_GLFW=1)
    endif()
endif()

if (WIN32)
    target_link_libraries(oxikara PRIVATE winmm)
endif()

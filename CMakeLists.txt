cmake_minimum_required(VERSION 3.15)

project(godot-spout VERSION 1.2.1)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(GODOT_SPOUT_ENABLE_VULKAN "Enable Spout Vulkan backend" OFF)
option(GODOT_SPOUT_ENABLE_OPENGL "Enable Spout OpenGL backend" OFF)

if(CMAKE_GENERATOR_PLATFORM MATCHES "arm64|aarch64|ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    set(ARCH "arm64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH "x86_64")
else()
    set(ARCH "x86_32")
endif()

# godot-cpp
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/godot-cpp")

# Spout2
set(SPOUT_NO_GLOBAL_WARNING ON CACHE BOOL "" FORCE)
set(SPOUT_BUILD_CMT OFF CACHE BOOL "" FORCE)
set(SPOUT_BUILD_SPOUTDX ON CACHE BOOL "" FORCE)
if(ARCH STREQUAL "arm64")
    set(SPOUT_BUILD_ARM ON CACHE BOOL "" FORCE)
endif()
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/Spout2" EXCLUDE_FROM_ALL)

if(GODOT_SPOUT_ENABLE_VULKAN)
    # Only the Vulkan headers and loader are needed: the interop code lives in
    # src/SpoutVulkanGpu.cpp. lib/SpoutVulkan is no longer compiled.
    find_package(Vulkan REQUIRED)
endif()

file(GLOB_RECURSE INCLUDES "src/*.hpp")
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_library(GodotSpout SHARED ${SOURCES} ${INCLUDES})

set_target_properties(GodotSpout PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "godot-spout.windows.$<IF:$<CONFIG:Debug>,debug,release>.${ARCH}"
)

target_include_directories(GodotSpout PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/lib/godot-cpp/gdextension
    ${PROJECT_SOURCE_DIR}/lib/godot-cpp/include
    ${PROJECT_SOURCE_DIR}/lib/godot-cpp/gen/include
    ${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX
    ${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX12
    # Spout protocol helpers (spoutSenderNames, spoutFrameCount, SpoutUtils),
    # compiled into SpoutDX_static and used by all backends.
    ${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/SpoutGL
)
if(ARCH STREQUAL "arm64")
    target_compile_options(GodotSpout PRIVATE /Zc:preprocessor)
    target_include_directories(GodotSpout PRIVATE
        ${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/sse2neon
    )
endif()

target_link_libraries(GodotSpout PRIVATE godot-cpp SpoutDX12_static d3d11 d3d12 dxgi)

if(GODOT_SPOUT_ENABLE_OPENGL)
    target_compile_definitions(GodotSpout PRIVATE GODOT_SPOUT_ENABLE_OPENGL)
    target_link_libraries(GodotSpout PRIVATE Spout_static)
endif()

if(GODOT_SPOUT_ENABLE_VULKAN)
    target_compile_definitions(GodotSpout PRIVATE
        GODOT_SPOUT_ENABLE_VULKAN VK_USE_PLATFORM_WIN32_KHR)
    target_link_libraries(GodotSpout PRIVATE Vulkan::Vulkan)
endif()

set(GODOT_SPOUT_ADDON_DIR "${PROJECT_SOURCE_DIR}/demo/addons/godot-spout")

install(TARGETS GodotSpout RUNTIME DESTINATION "${GODOT_SPOUT_ADDON_DIR}/bin")
install(FILES
    "${PROJECT_SOURCE_DIR}/README.md"
    "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION "${GODOT_SPOUT_ADDON_DIR}"
)
install(FILES "${PROJECT_SOURCE_DIR}/lib/Spout2/LICENSE"
    DESTINATION "${GODOT_SPOUT_ADDON_DIR}/licenses"
    RENAME "Spout2-LICENSE"
)
install(FILES "${PROJECT_SOURCE_DIR}/lib/SpoutVulkan/LICENSE"
    DESTINATION "${GODOT_SPOUT_ADDON_DIR}/licenses"
    RENAME "SpoutVulkan-LICENSE"
)

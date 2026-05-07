cmake_minimum_required(VERSION 3.15)

project(godot-spout VERSION 1.1.0)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
)
if(ARCH STREQUAL "arm64")
    target_compile_options(GodotSpout PRIVATE /Zc:preprocessor)
    target_include_directories(GodotSpout PRIVATE
        ${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/sse2neon
    )
endif()

target_link_libraries(GodotSpout PRIVATE godot-cpp SpoutDX12_static)

install(TARGETS GodotSpout RUNTIME DESTINATION ${PROJECT_SOURCE_DIR}/demo/addons/godot-spout/bin)

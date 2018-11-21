cmake_minimum_required(VERSION 3.1)

project(godot-spout VERSION 1.0.0)

option(BUILD_GDNATIVE "Build GDNative" ON)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BITS "64")
else()
    set(BITS "32")
endif()

find_package(OpenGL REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

include_directories(${PROJECT_SOURCE_DIR}/lib/Spout2/SpoutSDK/Source)
include_directories(${PROJECT_SOURCE_DIR}/lib/godot-cpp/godot_headers)
include_directories(${PROJECT_SOURCE_DIR}/lib/godot-cpp/include)
include_directories(${PROJECT_SOURCE_DIR}/lib/godot-cpp/include/core)
include_directories(${PROJECT_SOURCE_DIR}/lib/godot-cpp/include/gen)

link_directories(${PROJECT_SOURCE_DIR}/lib/godot-cpp/bin)

file(GLOB SPOUT_SOURCES ${PROJECT_SOURCE_DIR}/lib/Spout2/SpoutSDK/Source/*.cpp)
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h" "src/*.hpp")

add_library(Spout ${SPOUT_SOURCES})
add_library(SpoutPlugin SHARED ${SOURCES})

target_link_libraries(Spout opengl32)
target_link_libraries(SpoutPlugin Spout optimized libgodot-cpp.windows.release.${BITS} debug libgodot-cpp.windows.debug.${BITS})

if (BUILD_GDNATIVE)
    add_custom_command(
        TARGET SpoutPlugin
        PRE_BUILD
        COMMAND scons platform=windows target=$<$<CONFIG:Debug>:debug>$<$<CONFIG:Release>:release> bits=${BITS} generate_bindings=yes
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/lib/godot-cpp
    )
endif()

install(TARGETS SpoutPlugin RUNTIME DESTINATION SpoutPlugin)
install(FILES ${PROJECT_SOURCE_DIR}/res/plugin.cfg DESTINATION SpoutPlugin)
install(FILES ${PROJECT_SOURCE_DIR}/res/plugin.gd DESTINATION SpoutPlugin)
install(FILES ${PROJECT_SOURCE_DIR}/res/SpoutPlugin.gdnlib DESTINATION SpoutPlugin)
install(FILES ${PROJECT_SOURCE_DIR}/res/SpoutTexture.gdns DESTINATION SpoutPlugin)
install(FILES ${PROJECT_SOURCE_DIR}/res/SpoutViewport.gdns DESTINATION SpoutPlugin)

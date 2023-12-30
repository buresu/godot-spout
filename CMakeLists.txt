cmake_minimum_required(VERSION 3.1)

project(godot-spout VERSION 1.0.0)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /DTYPED_METHOD_BIND")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MT /DTYPED_METHOD_BIND")

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
include_directories(${PROJECT_SOURCE_DIR}/lib/godot-cpp/gdextension)
include_directories(${PROJECT_SOURCE_DIR}/lib/godot-cpp/include)
include_directories(${PROJECT_SOURCE_DIR}/lib/godot-cpp/gen/include)

link_directories(${PROJECT_SOURCE_DIR}/lib/godot-cpp/bin)

file(GLOB SPOUT_SOURCES ${PROJECT_SOURCE_DIR}/lib/Spout2/SpoutSDK/Source/*.cpp)
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h" "src/*.hpp")

add_library(Spout ${SPOUT_SOURCES})
add_library(SpoutPlugind SHARED ${SOURCES})

target_link_libraries(Spout opengl32)
target_link_libraries(SpoutPlugind Spout optimized libgodot-cpp.windows.template_release.x86_${BITS} debug libgodot-cpp.windows.template_debug.x86_${BITS})

add_custom_command(
    TARGET SpoutPlugind
    PRE_BUILD
    COMMAND scons platform=windows target=$<$<CONFIG:Debug>:template_debug>$<$<CONFIG:Release>:template_release> bits=${BITS}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/lib/godot-cpp
)

install(TARGETS SpoutPlugind RUNTIME DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/SpoutPlugin)
install(FILES ${PROJECT_SOURCE_DIR}/res/spout.gdextension DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/SpoutPlugin)

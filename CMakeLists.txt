cmake_minimum_required(VERSION 3.15)

# Must be set before project() to affect all subdirectory project() calls as well
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)

project(godot-spout VERSION 1.0.0)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /DTYPED_METHOD_BIND")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /DTYPED_METHOD_BIND")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BITS "64")
else()
    set(BITS "32")
endif()

# Required by Spout2's sub-CMakeLists for output directory settings
set(SPOUT_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/SpoutGL EXCLUDE_FROM_ALL)
add_subdirectory(${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX EXCLUDE_FROM_ALL)
add_subdirectory(${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX12 EXCLUDE_FROM_ALL)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h" "src/*.hpp")

add_library(GodotSpout SHARED ${SOURCES})

set_target_properties(GodotSpout PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "godot-spout.windows.$<IF:$<CONFIG:Debug>,debug,release>.x86_${BITS}"
)

target_include_directories(GodotSpout PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(GodotSpout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(GodotSpout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/godot-cpp/gdextension)
target_include_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/godot-cpp/include)
target_include_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/godot-cpp/gen/include)
target_include_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX)
target_include_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX12)

target_link_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/godot-cpp/bin)
target_link_libraries(GodotSpout SpoutDX12_static optimized libgodot-cpp.windows.template_release.x86_${BITS} debug libgodot-cpp.windows.template_debug.x86_${BITS})

add_custom_command(
    TARGET GodotSpout
    PRE_BUILD
    COMMAND scons platform=windows target=$<IF:$<CONFIG:Debug>,template_debug,template_release> bits=${BITS}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/lib/godot-cpp
)

install(TARGETS GodotSpout RUNTIME DESTINATION ${PROJECT_SOURCE_DIR}/demo/addons/godot-spout/bin)

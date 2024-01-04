cmake_minimum_required(VERSION 3.12)

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

add_subdirectory(${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/SpoutGL)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h" "src/*.hpp")

add_library(GodotSpout SHARED ${SOURCES})

set_target_properties(GodotSpout PROPERTIES DEBUG_POSTFIX d)

target_include_directories(GodotSpout PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(GodotSpout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(GodotSpout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/godot-cpp/gdextension)
target_include_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/godot-cpp/include)
target_include_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/godot-cpp/gen/include)
target_include_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/Spout2/SPOUTSDK/SpoutGL)

target_link_directories(GodotSpout PRIVATE ${PROJECT_SOURCE_DIR}/lib/godot-cpp/bin)
target_link_libraries(GodotSpout Spout_static optimized libgodot-cpp.windows.template_release.x86_${BITS} debug libgodot-cpp.windows.template_debug.x86_${BITS})

add_custom_command(
    TARGET GodotSpout
    PRE_BUILD
    COMMAND scons platform=windows target=$<$<CONFIG:Debug>:template_debug>$<$<CONFIG:Release>:template_release> bits=${BITS}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/lib/godot-cpp
)

install(TARGETS GodotSpout RUNTIME DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/bin)
install(FILES ${PROJECT_SOURCE_DIR}/res/spout.gdextension DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/bin)

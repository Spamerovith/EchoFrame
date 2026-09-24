cmake_minimum_required(VERSION 3.25)
project(EchoFrame VERSION 1.0.11 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(DEFINED ENV{GEODE_SDK})
    set(ECHOFRAME_GEODE_SDK "$ENV{GEODE_SDK}")
else()
    set(ECHOFRAME_GEODE_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../ИСХОДНИКИ/Autodeafen/vendor/geode-sdk")
endif()
if(NOT EXISTS "${ECHOFRAME_GEODE_SDK}/CMakeLists.txt")
    message(FATAL_ERROR "Geode SDK 5.10.1 not found; set GEODE_SDK")
endif()

set(GEODE_BINDINGS_REPO_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/../ИСХОДНИКИ/Autodeafen/vendor/bindings"
    CACHE PATH "Geode bindings")
set(GEODE_DISABLE_CLI_CALLS ON CACHE BOOL "Package separately" FORCE)
set(GEODE_DONT_BUILD_TEST_MODS ON CACHE BOOL "No SDK tests" FORCE)
add_subdirectory("${ECHOFRAME_GEODE_SDK}" "${CMAKE_BINARY_DIR}/sdk")

add_library(EchoFrame SHARED src/main.cpp src/extrapolation.cpp)
set_target_properties(EchoFrame PROPERTIES PREFIX "" OUTPUT_NAME "EchoFrame")
target_compile_definitions(EchoFrame PRIVATE GEODE_MOD_ID="echoframe.echoframe")
setup_geode_mod(EchoFrame)

set(ECHOFRAME_PACKAGER
    "${CMAKE_CURRENT_SOURCE_DIR}/../ИСХОДНИКИ/Autodeafen/build/geode.exe")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(EchoFramePackage
    COMMAND "${ECHOFRAME_PACKAGER}" package new "${CMAKE_CURRENT_SOURCE_DIR}"
        --binary "$<TARGET_FILE:EchoFrame>"
        --output "${CMAKE_CURRENT_SOURCE_DIR}/echoframe.geode"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/repack_geode.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/echoframe.geode"
    DEPENDS EchoFrame
    VERBATIM
)

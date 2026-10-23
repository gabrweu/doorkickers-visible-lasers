# era's Lights and Lasers, a Door Kickers 2 plugin for the Modloader (dk2ml). The project name names the DLL, the mod
# folder tools\install.ps1 and tools\package.ps1 create, and the loader's settings folder (GetConfigDir).
cmake_minimum_required(VERSION 3.20)
project(dk2_visible_lasers VERSION 3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: players need no VC++ redistributable. Build Release for the Workshop: Debug lays out std:: types
# differently from the game, and its CRT checks slow every frame.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Door Kickers 2 is 64-bit; configure with an x64 toolchain")
endif()

add_library(${PROJECT_NAME} SHARED
    src/Plugin.cpp
    src/game/Game.cpp
    src/lasers/Engagement.cpp
    src/lasers/Guns.cpp
    src/lasers/Lasers.cpp
    src/lasers/Lights.cpp
    src/lasers/Units.cpp
    src/ui/MenuButton.cpp
    src/ui/UnitButton.cpp
    src/ui/Window.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE dk2ml src ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE NOMINMAX PLUGIN_VERSION="${PROJECT_VERSION}")
target_link_libraries(${PROJECT_NAME} PRIVATE user32)

# BuildTime.h (PLUGIN_BUILD_TIME), rewritten every build: __TIME__ would keep the time of the file's last compile
set(BUILD_TIME_HEADER ${CMAKE_BINARY_DIR}/generated/BuildTime.h)
add_custom_target(build_time ALL
    COMMAND ${CMAKE_COMMAND} -DOUT=${BUILD_TIME_HEADER} -P ${CMAKE_SOURCE_DIR}/cmake/BuildTime.cmake
    BYPRODUCTS ${BUILD_TIME_HEADER})
add_dependencies(${PROJECT_NAME} build_time)

cmake_minimum_required(VERSION 3.16)
project(Upgrade LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# raylib 5.5, fetched by git so nobody installs anything by hand
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# ---- The game: every .cpp under src/ (adding a file needs no CMake edit)
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(upgrade ${GAME_SOURCES})
target_include_directories(upgrade PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(upgrade PRIVATE raylib)
if(APPLE)
  target_link_libraries(upgrade PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
if(MSVC)
  # no console window on Windows release builds is optional; keep the console for logs
  target_compile_definitions(upgrade PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Copy assets next to the executable after each build, so a zipped build folder runs.
add_custom_command(TARGET upgrade POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:upgrade>/assets)

# ---- Asset generator: draws every sprite sheet and synthesises every sound.
# The generated files are committed, so you only need this to change the art.
# Run:  cmake --build build --target assetgen && ./build/assetgen assets
add_executable(assetgen tools/assetgen/main.cpp tools/assetgen/SpriteGen.cpp tools/assetgen/Era4Art.cpp tools/assetgen/SoundGen.cpp)
target_include_directories(assetgen PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(assetgen PRIVATE raylib)
target_sources(assetgen PRIVATE tools/assetgen/Era4Sound.cpp)
if(APPLE)
  target_link_libraries(assetgen PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

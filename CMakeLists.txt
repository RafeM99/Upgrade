# Upgrade: GameJam 2026
# Owner: Systems (Camilla's Claude). Shared hotspot: keep edits small and say so in your status file.
cmake_minimum_required(VERSION 3.16)
project(Upgrade LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# raylib 5.5, fetched at configure time so every laptop builds the same version.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

# Every .cpp under src/ is compiled. New files are picked up on the next build.
file(GLOB_RECURSE UPGRADE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(upgrade ${UPGRADE_SOURCES})
target_include_directories(upgrade PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(upgrade PRIVATE raylib)

# While developing, assets load straight from the repo, so edits to chunks,
# config.ini and strings.txt show up without a rebuild.
target_compile_definitions(upgrade PRIVATE UPGRADE_SOURCE_ASSETS="${CMAKE_SOURCE_DIR}/assets/")

if(APPLE)
  target_link_libraries(upgrade PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(upgrade PRIVATE -Wall -Wextra)
endif()

# Copy assets next to the executable, so a build folder can be zipped and shipped.
add_custom_command(TARGET upgrade POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:upgrade>/assets)

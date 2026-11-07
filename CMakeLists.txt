cmake_minimum_required(VERSION 3.16)
project(Upgrade LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release for single-config generators
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# raylib 5.5, fetched by git
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

file(GLOB_RECURSE UPGRADE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(upgrade ${UPGRADE_SOURCES})
target_include_directories(upgrade PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(upgrade PRIVATE raylib)

# Assets: read from the source folder while developing, else from a copy next to the executable
target_compile_definitions(upgrade PRIVATE UPGRADE_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/")
add_custom_command(TARGET upgrade POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:upgrade>/assets)

if(APPLE)
  target_link_libraries(upgrade PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

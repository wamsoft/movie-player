# MSVC_RUNTIME_LIBRARY のサポートが 3.15 から
cmake_minimum_required(VERSION 3.15.0)

if(UNIX AND NOT APPLE)
  set(LINUX TRUE)
endif()

# vcpkg対応
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "")
endif()

# ------------------------------------------------------------------------------
# movie_player_test
# ------------------------------------------------------------------------------
if(WIN32 OR LINUX)

set(MOVIE_PLAYER_TEST movie_player_test)
project(${MOVIE_PLAYER_TEST} VERSION 0.1.0)
add_executable(${MOVIE_PLAYER_TEST} GLRenderer.cpp movie_player_test.cpp)

# set(MOVIE_PLAYER_TEST webmtest)
# project(${MOVIE_PLAYER_TEST} VERSION 0.1.0)
# add_executable(${MOVIE_PLAYER_TEST} webm_test.cpp)

# movieplayer の CMake
add_subdirectory(../../ movieplayer)
target_link_libraries(${MOVIE_PLAYER_TEST} PRIVATE movieplayer)
set(LIB_MOVIE_PLAYER_LOADED TRUE)

if(MSVC)
  # 各種warnを潰す
  target_compile_options(${MOVIE_PLAYER_TEST} PRIVATE 
    /utf-8 /wd4819 /wd4996 /wd4244 /wd4018 /wd4267 /MP)
endif()

# vcpkg で glfw/glfw をリンク
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(${MOVIE_PLAYER_TEST} PRIVATE GLEW::GLEW)
target_link_libraries(${MOVIE_PLAYER_TEST} PRIVATE glfw)
endif()

# ------------------------------------------------------------------------------
# movie_exporter
# ------------------------------------------------------------------------------
# 必要なときだけONにしてビルドする
if(OFF)
set(MOVIE_EXPORTER movie_exporter)
project(${MOVIE_EXPORTER} VERSION 0.1.0)
add_executable(${MOVIE_EXPORTER} movie_exporter.cpp)

# movieplayer の CMake
if(NOT LIB_MOVIE_PLAYER_LOADED)
add_subdirectory(../../ movieplayer)
endif()
target_link_libraries(${MOVIE_EXPORTER} PRIVATE movieplayer)

if(MSVC)
  # 各種warnを潰す
  target_compile_options(${MOVIE_EXPORTER} PRIVATE 
    /utf-8 /wd4819 /wd4996 /wd4244 /wd4018 /wd4267 /MP)
endif()

# std::filesystem のために C++17
target_compile_features(${MOVIE_EXPORTER} PRIVATE cxx_std_17)

# /MT(d) でコード生成
# set_property(TARGET ${PROJ_NAME} 
#   PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
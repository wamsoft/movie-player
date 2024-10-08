cmake_minimum_required(VERSION 3.10.2)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

# ------------------------------------------------------------------------------
# vcpkg環境
# ------------------------------------------------------------------------------
if(WIN32 OR LINUX)

# vcpkg対応
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
	CACHE STRING "")
endif()

endif()

# ------------------------------------------------------------------------------
# プロジェクト名設定
# ------------------------------------------------------------------------------
set(PROJ_NAME movieplayer)
project(${PROJ_NAME})

# ------------------------------------------------------------------------------
# Win64チェック / 必ず project() 以後に呼ぶ
#   ${CMAKE_GENERATOR_PLATFORM}はcmake実行時に-Aオプションで渡してやらないと
#   入らないようなので、WIN32チェック＋ポインタサイズチェックで判定する
# ------------------------------------------------------------------------------
if(WIN32 AND (CMAKE_SIZEOF_VOID_P EQUAL 8))
set(WIN64 1)
endif()

# ------------------------------------------------------------------------------
# 共通
# ------------------------------------------------------------------------------
# for libyuv
# if (TEST) の TEST を演算子とみなすかどうか。yuv では OPTION 変数として使ってるので
# 一時的に OLD 動作に固定しておく。ポリシはサブに伝播しないのでデフォルトを変更している。
# (yuv の CMakeLists.txt いじればいいのでは？ というのは、それはそう)
#   Win64はSSE/AVXコードがVSビルドに対応していない関係でprebuildしたバイナリを
#   参照するので、yuvのCMakeLists.txtは参照しない
if(NOT WIN64)

cmake_policy(GET CMP0064 pre_cmp0064)
set(CMAKE_POLICY_DEFAULT_CMP0064 OLD)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/extlibs/libyuv)
cmake_policy(SET CMP0064 ${pre_cmp0064})

endif()

# ------------------------------------------------------------------------------
# nestegg版対応アーキテクチャ共通
# ------------------------------------------------------------------------------
if(WIN32 OR LINUX)

if(WIN32)
# ※add_compile_options は add_library()/add_executable() の前に
#   呼ぶ必要があるので、「共通」と言いつつこの指定だけはここへ配置
# unicode 警告無視
add_compile_options(/wd4819 /wd4244 /wd4267)
endif()

add_library(${PROJ_NAME} STATIC
	src/common/MessageLooper.cpp
	src/common/PixelConvert.cpp
	src/common/AudioEngine.cpp
	src/common/MediaClock.cpp
	src/windows/Decoder.cpp
	src/windows/VpxDecoder.cpp
	src/windows/VorbisDecoder.cpp
	src/windows/OpusDecoder.cpp
	src/windows/WebmExtractor.cpp
	src/windows/MkvFileReader.cpp
	src/windows/MkvStreamReader.cpp
	src/windows/MoviePlayerCore.cpp
	src/windows/MoviePlayer.cpp
	extlibs/nestegg/src/nestegg.c
)

target_compile_definitions(${PROJ_NAME} PUBLIC
	INNER_AUDIOENGINE=1)

target_include_directories(${PROJ_NAME} 
PUBLIC 
	include
PRIVATE
	src/windows
	extlibs/nestegg/include
	extlibs/libyuv/include
	)

target_include_directories(${PROJ_NAME} PRIVATE
	src/common
	src/windows
	)

# vpx
find_package(unofficial-libvpx CONFIG REQUIRED)
target_link_libraries(${PROJ_NAME} PUBLIC unofficial::libvpx::libvpx)

# libyuv(extlibs/配下のものを参照する)
# SSE/AVX対応コードの64bitビルドはVSでは対応していないので、
# clangで作成済みのバイナリをリンクする
if(WIN64)
message("*** Target is WIN64. Use prebuild libyuv-x64 binary. ***")
find_library(LIBYUV_WIN64_DBG yuv_debug ${CMAKE_CURRENT_LIST_DIR}/extlibs/prebuild/win64)
find_library(LIBYUV_WIN64_REL yuv_release ${CMAKE_CURRENT_LIST_DIR}/extlibs/prebuild/win64)
target_link_libraries(${PROJ_NAME} PUBLIC
	debug ${LIBYUV_WIN64_DBG}
	optimized ${LIBYUV_WIN64_REL})
else()
target_link_libraries(${PROJ_NAME} PUBLIC yuv)
endif()

# ogg
find_package(Ogg CONFIG REQUIRED)
target_link_libraries(${PROJ_NAME} PUBLIC Ogg::ogg)

# vorbis
find_package(Vorbis CONFIG REQUIRED)
target_link_libraries(${PROJ_NAME} PUBLIC Vorbis::vorbis)

# opus
find_package(Opus CONFIG REQUIRED)
target_link_libraries(${PROJ_NAME} PUBLIC Opus::opus)

#  miniaudio
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")
target_include_directories(${PROJ_NAME} PRIVATE ${MINIAUDIO_INCLUDE_DIRS})

endif()

# ------------------------------------------------------------------------------
# nestegg版対応アーキテクチャ個別：Linux
# ------------------------------------------------------------------------------
if(LINUX)

# pthread のリンクエラーを解消する
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${PROJ_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})

endif()

# ------------------------------------------------------------------------------
# Android
# ------------------------------------------------------------------------------
if(ANDROID)

add_library(${PROJ_NAME} STATIC
	src/common/MessageLooper.cpp
	src/common/PixelConvert.cpp
	src/android/TrackPlayer.cpp
	src/android/VideoTrackPlayer.cpp
	src/android/AudioTrackPlayer.cpp
	src/android/MoviePlayerCore.cpp
	src/android/MoviePlayer.cpp
	)

target_include_directories(${PROJ_NAME} 
PUBLIC 
	include
PRIVATE
	src/android
	extlibs/libyuv/include
)

target_include_directories(${PROJ_NAME} PRIVATE
	src/common
	)

find_library(log-lib log)

target_link_libraries(${PROJ_NAME}
	${log-lib}
	android
	mediandk
	OpenMAXAL
	yuv
	)

endif()

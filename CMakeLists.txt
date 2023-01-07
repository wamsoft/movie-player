cmake_minimum_required(VERSION 3.10.2)

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
# libwem版対応アーキテクチャ共通
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
	src/windows/Decoder.cpp
	src/windows/VpxDecoder.cpp
	src/windows/WebmExtractor.cpp
	src/windows/MkvFileReader.cpp
	src/windows/MoviePlayerCore.cpp
	src/windows/MoviePlayer.cpp)

target_include_directories(${PROJ_NAME} 
PUBLIC 
	include
PRIVATE
	src/windows
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
# clangで作成済みのバリナリをリンクする
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
# find_package(Ogg CONFIG REQUIRED)
# target_link_libraries(${PROJ_NAME} PUBLIC Ogg::ogg)

# vorbis
# find_package(Vorbis CONFIG REQUIRED)
# target_link_libraries(${PROJ_NAME} PUBLIC Vorbis::vorbis Vorbis::vorbisfile)
# target_link_libraries(${PROJ_NAME} PUBLIC Vorbis::vorbis Vorbis::vorbisenc Vorbis::vorbisfile)

# opus
# find_package(Opus CONFIG REQUIRED)
# target_link_libraries(${PROJ_NAME} PUBLIC Opus::opus)

endif()

# ------------------------------------------------------------------------------
# libwem版対応アーキテクチャ個別：Windows
# ------------------------------------------------------------------------------
if(WIN32)

# webm は vcpkg にあるが find_package 用のコンフィグがない
# ※vcpkgのwebmは、staticのときはlibwebm.lib、dynamicのときはwebm.libとなるので
#   真面目にやるならtripletを見て対応を振り分けてやる必要がある。
if(${VCPKG_TARGET_TRIPLET} MATCHES "static")
find_library(LIBWEBM libwebm)
target_link_libraries(${PROJ_NAME} PUBLIC ${LIBWEBM})
target_compile_definitions(${PROJ_NAME} PUBLIC WEBM_STATIC)
else()
find_library(LIBWEBM webm)
target_link_libraries(${PROJ_NAME} PUBLIC ${LIBWEBM})
endif()


endif()

# ------------------------------------------------------------------------------
# libwem版対応アーキテクチャ個別：Linux
# ------------------------------------------------------------------------------
if(LINUX)

# pthread のリンクエラーを解消する
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${PROJ_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})

# Linux 版は webm.a (vcpkgのlinux向けtripletには-staticがないので
# 多分windowsのdynamicと同じロジックでこの名前になってるのだと思われる)
find_library(LIBWEBM webm)
target_link_libraries(${PROJ_NAME} PUBLIC ${LIBWEBM})
# libwebm のヘッダで、相対ではなくルートからのパスで include する部分があり
# linux だとこれが見つけられずにコケてしまうので明示的に include パスを追加する
# (mkvparser/ 以下のヘッダで "mkvparser/xxxx.h" をincludeしてる)
target_include_directories(${PROJ_NAME} PUBLIC
	"$ENV{VCPKG_ROOT}/installed/x64-linux/include/libwebm"
)

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

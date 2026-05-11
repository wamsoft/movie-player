# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Standalone WebM playback library (target name `movieplayer`, static lib). Used by 吉里吉里Z (`krkrz`) as `external/movie-player`, but builds and tests on its own. Two backend implementations, selected at configure time by the target platform:

- **Android** (`src/android/`) — NDK `AMediaExtractor` + `AMediaCodec` (stagefright-style Extractor → Decoder pipeline). Requires API 29+ (uses `AMediaDataSource`). Each `MoviePlayer` spawns its own decoder `std::thread`.
- **Generic / non-Android** (`src/windows/`, *despite the name*) — `nestegg` for demux + `libvpx` (VP8/VP9) / `libvorbis` / `libopus` for decode, pure CPU. Works on Windows, Linux, macOS, and any vcpkg-supported target; the directory keeps its `windows/` name only for historical reasons (README notes a future rename to `generic/`).

`src/common/` is shared by both backends (`MessageLooper`, `MediaClock`, `PixelConvert`).

The library performs **no audio output of its own** — the host implements `IAudioSink` and passes it via `IMoviePlayer::InitParam::audioSink`. `nullptr` is legal and means "play without sound." This is uniform across all backends (Windows / Linux / macOS / Android).

## Common commands

CMake presets + Ninja Multi-Config, vcpkg toolchain. `VCPKG_ROOT` must be set.

```bash
# Direct cmake (see CMakePresets.json for the full preset list)
cmake --preset x64-windows
cmake --build build/x64-windows --config Release

# Via Makefile wrapper (auto-picks preset by OS/arch, BUILD_TYPE=Release, BUILD_TEST=ON by default)
make prebuild       # cmake configure (runs vcpkg install)
make build          # cmake --build
make clean

# Override preset / build type / extra cmake args
PRESET=x64-linux BUILD_TYPE=Debug CMAKEOPT="-DBUILD_TEST=OFF" make prebuild build
```

Presets: `x86-windows`, `x64-windows`, `x64-linux`, `x64-osx`, `arm64-osx`, `arm64-android`, `x64-android`.

Build flag worth knowing: `-DBUILD_TEST=ON` adds `test/windows/CMakeLists.txt` (builds `movie_player_test` and `movie_exporter`). The Makefile sets this by default; the bare `cmake --preset ...` invocations in README.md do not. The test subdirectory needs `glew` and `glfw3` from vcpkg in addition to the runtime deps.

## Tests / smoke runs

No automated test target. The two manual tests under `test/windows/` (built when `BUILD_TEST=ON`):

- **`movie_player_test <file.webm>`** — GLFW + GLEW renderer. Keys: LEFT/RIGHT = 2 s seek, SPACE = pause toggle, ENTER = rewind, ESC = stop, mouse wheel = volume. **Seek snaps to the nearest preceding keyframe (Cue point)** — if cues are 5 s apart, a 2 s seek will appear to do nothing. Audio uses `WaveOutAudioSink` (winmm) as a sample `IAudioSink`.
- **`movie_exporter <file.webm>`** — dumps a BMP every ~1 s of media time.

Test fixture WebMs live in `test/dat/` (gitignored — supplied out-of-band).

Android tests live under `test/android/` as an Android Studio project that references the top-level `CMakeLists.txt` through the NDK. `test/android/Makefile` wraps `gradlew` for command-line workflows: `make build` / `make install` / `make run` / `make logcat`. The app uses `AAudioSink` (`test/android/app/src/main/cpp/AAudioSink.h`) as its `IAudioSink` implementation — AAudio (API 26+) only, no external deps. The sink runs in `AAUDIO_PERFORMANCE_MODE_NONE` (not `LOW_LATENCY` — see the Android backend section below for why) and verifies actual rate / channels / format against the requested values after `openStream`. It also emits underrun counts to `logcat` (tag `AAudioSink`) which is the fastest way to diagnose audio-pacing issues. Surface display uses a `SurfaceView` + Bitmap path in `TestMovieView.java`; the bitmap is drawn with aspect-preserving letterbox to fit the surface.

## Architecture

### Public API

`include/IMoviePlayer.h` and `include/IAudioSink.h` are the *only* public headers; everything else is implementation detail.

```cpp
IMoviePlayer::InitParam p; p.Init();
p.videoColorFormat = IMoviePlayer::COLOR_BGRA;   // pick the format you want for RenderFrame
p.audioSink        = &myAudioSinkImpl;            // or nullptr for silent playback
IMoviePlayer *mp = IMoviePlayer::CreateMoviePlayer("foo.webm", p);
mp->SetOnState(...); mp->SetOnVideoDecoded(...);
mp->Play();
```

`CreateMoviePlayer` is overloaded for both `const char *filename` and `IMovieReadStream *` (a refcounted stream abstraction the host implements when bytes don't live on the filesystem). The factory is defined in each backend's `MoviePlayer.cpp` — only one is linked per build.

`COLOR_NOCONV` (`= COLOR_UNKNOWN`) means "pass the decoder's native output straight through" (YUV planar/semi-planar). Otherwise the library converts to one of the packed RGBA orderings via `PixelConvert.cpp` (which dispatches to libyuv). Note the byte-order convention spelled out in `IMoviePlayer.h`: e.g. `COLOR_BGRA` matches `GL_BGRA` / `DXGI_FORMAT_B8G8R8A8_UNORM` but is the *inverse* of libyuv's `xxxToARGB` naming.

### IAudioSink contract

Host implements `Setup` / `Enqueue` / `Start` / `Stop` / `GetSamplesPlayed` / `TryPopConsumed` / `Flush` / `SetVolume` / `Volume`. Lifetime rule: the `data` pointer passed to `Enqueue` stays valid until the sink reports it via `TryPopConsumed` — the buffer is owned by the movie-player's internal `DecodedBuffer` until then. `GetSamplesPlayed()` returns a monotonically-increasing device-clock sample count and is the **anchor source for `MediaClock`** when audio is present (audio-master sync). With `audioSink == nullptr` the player switches to video-master sync via `MoviePlayerCore::PropagateSyncMode`.

Reference implementations:
- `test/windows/WaveOutAudioSink.h` — Windows / `waveOut*`. Keeps the borrowed pointer until WOM_DONE.
- `test/android/app/src/main/cpp/AAudioSink.h` — Android / AAudio. **Copies the PCM in `Enqueue` and immediately marks `param` consumed**, releasing the codec output slot back to AMediaCodec right away. This decoupling is the recommended pattern for `AMediaCodec`-based audio paths — see the "Android backend pipeline" note below.

### Generic backend pipeline (src/windows/)

```
IMovieReadStream / file
        │
        ▼
   MkvFileReader / MkvStreamReader   (nestegg io_t adapter)
        │
        ▼
   WebmExtractor   (nestegg demux → FramePacket per track)
        │           ┌─ TRACK_TYPE_VIDEO ──► VpxDecoder           ─┐
        ├──Advance──┤                                              │  DecodedBuffer
        │           └─ TRACK_TYPE_AUDIO ──► VorbisDecoder/OpusDecoder ─┤  (planar YUV[+A] or S16 PCM)
        │                                                            │
        ▼                                                            ▼
   MoviePlayerCore (state machine on a MessageLooper thread; owns MediaClock)
        │                              │
        ├──video frame──► OnVideoDecoded callback (host uploads texture)
        └──audio PCM ───► IAudioSink::Enqueue
```

Each `Decoder` is itself a `MessageLooper`-derived worker thread with two `BufferQueue<T>` slots (input `FramePacket`s, output `DecodedBuffer`s). `MoviePlayerCore` runs a third worker thread that pumps the extractor, dispatches packets to the right decoder, drains decoded buffers, runs `DrainAudioSinkConsumed` to release sink-consumed buffers and re-anchor `MediaClock`, and decides when to publish the next video frame to the host via `OnVideoDecoded`. State machine lives in `MoviePlayerCore::HandleMessage` (states: UNINIT → OPEN → PRELOADING → PLAY ⇄ PAUSE → STOP/FINISH).

### Android backend pipeline (src/android/)

`MoviePlayerCore` owns one `VideoTrackPlayer` and one `AudioTrackPlayer`, each a `TrackPlayer` subclass wrapping an `AMediaCodec` + dedicated thread. Audio decoded PCM is forwarded to the same host-provided `IAudioSink` (the internal `AAudioStream`-based path has been removed — confirmed by `git log` reverting an internal `AudioEngine`). `MediaClock` sync mode (audio-master vs video-master) is decided in `PropagateSyncMode()` after both tracks have been set up.

**`IAudioSink` Enqueue lifetime gotcha (Android)** — `AudioTrackPlayer::HandleOutputData` passes the codec output buffer pointer to `IAudioSink::Enqueue` as `data`, with `bufIdx` as `param`. The codec slot is released only after the sink reports it back via `TryPopConsumed`. AMediaCodec audio decoders typically expose only **2–4 output slots**, so a naive sink that keeps the borrowed pointer until the device has played the audio will pin every slot and propagate backpressure to the input side: `TrackPlayer::ProcessInput`'s `AMediaCodec_dequeueInputBuffer(..., 10000us)` then stalls for the full 10 ms per iteration, decoder pacing collapses, and `mPending` runs dry. The symptom is asymmetric: video tempo stays correct (the audio clock advances on AAudio device-clock anchored by `getTimestamp`, which keeps moving even when we feed silence) but the *audible* audio is sparse and feels slowed-down. **Sinks for the Android path should copy the PCM in `Enqueue` and queue `param` as consumed immediately** — see `test/android/app/src/main/cpp/AAudioSink.h`.

### Shared building blocks (src/common/)

- **`MessageLooper`** — `std::thread` + `std::queue<Message>` + `condition_variable`. Method renamed from `PostMessage` to `Post` because `<windows.h>` macros clobber it.
- **`MediaClock`** — multi-anchor clock (start time, anchor time, max-media time, playback rate). The "what time is it?" source of truth for A/V sync. Same class is used by both backends.
- **`PixelConvert`** — `convert_yuv_to_rgb32(...)` wrapping libyuv. Two overloads; the `DecodedBuffer` overload is `!defined(__ANDROID__)` because Android's `DecodedBuffer` lives in a different namespace.

### Why the Windows x64 libyuv prebuilt binaries exist

`extlibs/libyuv/` is vendored. libyuv's SSE/AVX paths are inline assembly that **MSVC cannot build in 64-bit** (32-bit inline asm only). For `WIN64`, CMakeLists skips `add_subdirectory(.../libyuv)` and instead links the precompiled artifacts in `extlibs/prebuild/win64/` (built with clang-cl, currently VS2019 + Clang 12). For all other targets (Win32, Linux, macOS, Android), libyuv builds from source. To refresh the binaries, run `extlibs/make_prebuild.sh` from a vcvars64-equipped MSYS/Cygwin shell.

### Known issues (from README)

- Pause/resume leaves a few frames classed as frame-skips after resume.
- YUV texture pass-through (non-ARGB color formats) may misbehave.
- WSL/WSLg: `movie_player_test` runs but renders incorrectly.

## Conventions

C++17. Sources are UTF-8 and MSVC is forced to `/utf-8` with `_CRT_SECURE_NO_WARNINGS` and `/wd4819` (Japanese-in-source warning) globally suppressed in `CMakeLists.txt`. Comments and many log messages are Japanese.

`src/windows/Constants.h` defines the shared `TrackType` / `CodecId` enums; `src/android/Constants.h` is its Android counterpart. Don't confuse them — they live in different translation-unit groups.

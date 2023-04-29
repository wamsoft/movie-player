#include "MoviePlayer.h"
#include "Constants.h"
#include "MoviePlayerCore.h"

// TODO 本当に必要なもののみにする
// #include <stdio.h>
// #include <errno.h>
// #include <inttypes.h>
// #include <sys/time.h>
// #include <unistd.h>

#include "media/NdkMediaCrypto.h"
#include "media/NdkMediaCodec.h"
#include "media/NdkMediaError.h"
#include "media/NdkMediaFormat.h"
#include "media/NdkMediaExtractor.h"
#include "media/NdkMediaDataSource.h"
#include <android/asset_manager.h>

#include <android/log.h>
#define TAG       "MoviePlayer"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define ASSERT(_cond, ...) \
  if (!(_cond))            \
  __android_log_assert("conditional", TAG, __VA_ARGS__)

static inline PixelFormat
conv_color_format(IMoviePlayer::ColorFormat colorFormat)
{
  PixelFormat pixelFormat = PIXEL_FORMAT_UNKNOWN;

  switch (colorFormat) {
  case IMoviePlayer::UNKNOWN:
    pixelFormat = PIXEL_FORMAT_UNKNOWN;
    break;
  case IMoviePlayer::ABGR:
    pixelFormat = PIXEL_FORMAT_ABGR;
    break;
  case IMoviePlayer::ARGB:
    pixelFormat = PIXEL_FORMAT_ARGB;
    break;
  case IMoviePlayer::RGBA:
    pixelFormat = PIXEL_FORMAT_RGBA;
    break;
  case IMoviePlayer::BGRA:
    pixelFormat = PIXEL_FORMAT_BGRA;
    break;
  case IMoviePlayer::I420:
    pixelFormat = PIXEL_FORMAT_I420;
    break;
  case IMoviePlayer::NV12:
    pixelFormat = PIXEL_FORMAT_NV12;
    break;
  case IMoviePlayer::NV21:
    pixelFormat = PIXEL_FORMAT_NV21;
    break;
  default:
    ASSERT(false, "unknown color format: %d\n", colorFormat);
    break;
  }

  return pixelFormat;
}

// -----------------------------------------------------------------------------
// MoviePlayer
// -----------------------------------------------------------------------------
MoviePlayer::MoviePlayer()
: mPlayer(nullptr)
, mAsset(nullptr)
{
  Init();
}

MoviePlayer::~MoviePlayer()
{
  Done();
}

void
MoviePlayer::Init()
{}

void
MoviePlayer::Done()
{
  if (mPlayer != nullptr) {
    mPlayer->Stop();
    delete mPlayer;
    mPlayer = nullptr;
  }

  if (mAsset) {
    AAsset_close(mAsset);
    mAsset = nullptr;
  }
}

bool
MoviePlayer::Open(const char *filepath)
{
  mPlayer = new MoviePlayerCore();
  mPlayer->SetPixelFormat(conv_color_format(mColorFormat));
  return mPlayer->Open(filepath);
}

bool
MoviePlayer::Open(int fd, off_t offset, off_t length)
{
  mPlayer = new MoviePlayerCore();
  mPlayer->SetPixelFormat(conv_color_format(mColorFormat));
  return mPlayer->Open(fd, offset, length);
}

bool
MoviePlayer::Open(AAssetManager *manager, const char *filepath)
{
  if ((mAsset = AAssetManager_open(manager, filepath, AASSET_MODE_RANDOM))) {
    int fd;
    off_t offset;
    off_t length;
    if ((fd = AAsset_openFileDescriptor(mAsset, &offset, &length)) >= 0) {
      return Open(fd, offset, length);
    }
  }
  return false;
}

void
MoviePlayer::Play(bool loop)
{
  LOGV("play");

  if (mPlayer) {
    return mPlayer->Play(loop);
  }
}

void
MoviePlayer::Stop()
{
  LOGV("stop");

  if (mPlayer) {
    return mPlayer->Stop();
  }
}

void
MoviePlayer::Pause()
{
  LOGV("pause");

  if (mPlayer) {
    return mPlayer->Pause();
  }
}

void
MoviePlayer::Resume()
{
  LOGV("resume");

  if (mPlayer) {
    return mPlayer->Resume();
  }
}

void
MoviePlayer::Seek(int64_t posUs)
{
  LOGV("seek posUs=%lld", posUs);

  if (mPlayer) {
    return mPlayer->Seek(posUs);
  }
}

void
MoviePlayer::SetLoop(bool loop)
{
  LOGV("set loop=%d", loop);

  if (mPlayer) {
    return mPlayer->SetLoop(loop);
  }
}

void
MoviePlayer::SetColorFormat(ColorFormat format)
{
  LOGV("set ColorFormat=%d", format);

  IMoviePlayer::SetColorFormat(format);
  if (mPlayer) {
    mPlayer->SetPixelFormat(conv_color_format(mColorFormat));
  }
}

int32_t
MoviePlayer::Width() const
{
  if (mPlayer) {
    return mPlayer->Width();
  } else {
    return -1;
  }
}

int32_t
MoviePlayer::Height() const
{
  if (mPlayer) {
    return mPlayer->Height();
  } else {
    return -1;
  }
}

bool
MoviePlayer::IsAudioAvailable() const
{
  if (mPlayer) {
    return mPlayer->IsAudioAvailable();
  } else {
    return false;
  }
}

void
MoviePlayer::GetAudioFormat(AudioFormat *format) const
{
  if (IsAudioAvailable() && format != nullptr) {
    format->sampleRate     = mPlayer->SampleRate();
    format->channels       = mPlayer->Channels();
    format->bitsPerSample  = mPlayer->BitsPerSample();
    format->maxInputSize   = mPlayer->MaxInputSize();
    int32_t nativeEncoding = mPlayer->Encoding();
    switch (nativeEncoding) {
    case kAudioEncodingPcm16bit:
      format->encoding = PCM_16;
      break;
    case kAudioEncodingPcm8bit:
      format->encoding = PCM_8;
      break;
    case kAudioEncodingPcmFloat:
      format->encoding = PCM_FLOAT;
      break;
    case kAudioEncodingPcm32bit:
      format->encoding = PCM_32;
      break;
    default:
      ASSERT(false, "unsupported audio encoding: %d", nativeEncoding);
      break;
    }
  }
}

int64_t
MoviePlayer::Duration() const
{
  if (mPlayer) {
    return mPlayer->Duration();
  } else {
    return -1;
  }
}

int64_t
MoviePlayer::Position() const
{
  if (mPlayer) {
    return mPlayer->Position();
  } else {
    return -1;
  }
}

bool
MoviePlayer::IsPlaying() const
{
  if (mPlayer) {
    return mPlayer->IsPlaying();
  } else {
    return false;
  }
}

bool
MoviePlayer::Loop() const
{
  if (mPlayer) {
    return mPlayer->Loop();
  } else {
    return false;
  }
}

void
MoviePlayer::SetOnAudioDecoded(OnAudioDecoded func, void *userPtr)
{
  IMoviePlayer::SetOnAudioDecoded(func, userPtr);
  if (mPlayer) {
    mPlayer->SetOnAudioDecoded(func, userPtr);
  }
}

void
MoviePlayer::RenderFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                         ColorFormat format)
{
  if (!mPlayer) {
    LOGE("internal player is not running.");
    return;
  }

  if (!IsPlaying()) {
    LOGV("internal player is not playing now.");
    return;
  }

  PixelFormat internalFormat = PIXEL_FORMAT_UNKNOWN;
  switch (format) {
  case UNKNOWN:
    // UNKNOWNはMoviePlayer::SetColorFormat()で有効なカラーをデフォルトとして
    // 設定している場合のみ有効な指定。
    if (mColorFormat == MoviePlayer::UNKNOWN) {
      ASSERT(
        false,
        "if you specify MoviePlayer::UNKNOWN color format for MoviePlayer::RenderFrame(),"
        " you MUST set default color format with MoviePlayer::SetColorFormat().\n");
      return;
    }
    break;
  case ABGR:
    internalFormat = PIXEL_FORMAT_ABGR;
    break;
  case ARGB:
    internalFormat = PIXEL_FORMAT_ARGB;
    break;
  case RGBA:
    internalFormat = PIXEL_FORMAT_RGBA;
    break;
  case BGRA:
    internalFormat = PIXEL_FORMAT_BGRA;
    break;
  case I420:
  case NV12:
  case NV21:
    ASSERT(false, "yuv format not supported for MoviePlayer::RenderFrame().\n");
    break;
  default:
    ASSERT(false, "unknown color format: %d\n", format);
    break;
  }

  mPlayer->RenderFrame(dst, w, h, strideBytes, internalFormat);
}

IMoviePlayer *
IMoviePlayer::CreateMoviePlayer(const char *filename, ColorFormat format)
{
  MoviePlayer *player = new MoviePlayer();
  player->SetColorFormat(format);
  if (player->Open(filename)) {
    return player;
  }
  delete player;
  return nullptr;
}

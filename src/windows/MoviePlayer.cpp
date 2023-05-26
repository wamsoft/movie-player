#define MYLOG_TAG "MoviePlayer"
#include "BasicLog.h"
#include "MoviePlayer.h"
#include "MoviePlayerCore.h"
#include "PixelConvert.h"

#include <cstdint>
#include <cstdlib>

static inline PixelFormat
conv_color_format(IMoviePlayer::ColorFormat colorFormat)
{
  PixelFormat pixelFormat = PIXEL_FORMAT_UNKNOWN;

  switch (colorFormat) {
  case IMoviePlayer::COLOR_UNKNOWN:
    pixelFormat = PIXEL_FORMAT_UNKNOWN;
    break;
  case IMoviePlayer::COLOR_ABGR:
    pixelFormat = PIXEL_FORMAT_ABGR;
    break;
  case IMoviePlayer::COLOR_ARGB:
    pixelFormat = PIXEL_FORMAT_ARGB;
    break;
  case IMoviePlayer::COLOR_RGBA:
    pixelFormat = PIXEL_FORMAT_RGBA;
    break;
  case IMoviePlayer::COLOR_BGRA:
    pixelFormat = PIXEL_FORMAT_BGRA;
    break;
  case IMoviePlayer::COLOR_I420:
    pixelFormat = PIXEL_FORMAT_I420;
    break;
  case IMoviePlayer::COLOR_NV12:
    pixelFormat = PIXEL_FORMAT_NV12;
    break;
  case IMoviePlayer::COLOR_NV21:
    pixelFormat = PIXEL_FORMAT_NV21;
    break;
  default:
    ASSERT(false, "unknown color format: %d\n", colorFormat);
    break;
  }

  return pixelFormat;
}

static inline IMoviePlayer::ColorFormat
conv_pixel_format(PixelFormat pixelFormat)
{
  IMoviePlayer::ColorFormat colorFormat = IMoviePlayer::COLOR_UNKNOWN;

  switch (colorFormat) {
  case PIXEL_FORMAT_UNKNOWN:
    colorFormat = IMoviePlayer::COLOR_UNKNOWN;
    break;
  case PIXEL_FORMAT_ABGR:
    colorFormat = IMoviePlayer::COLOR_ABGR;
    break;
  case PIXEL_FORMAT_ARGB:
    colorFormat = IMoviePlayer::COLOR_ARGB;
    break;
  case PIXEL_FORMAT_RGBA:
    colorFormat = IMoviePlayer::COLOR_RGBA;
    break;
  case PIXEL_FORMAT_BGRA:
    colorFormat = IMoviePlayer::COLOR_BGRA;
    break;
  case PIXEL_FORMAT_I420:
    colorFormat = IMoviePlayer::COLOR_I420;
    break;
  case PIXEL_FORMAT_NV12:
    colorFormat = IMoviePlayer::COLOR_NV12;
    break;
  case PIXEL_FORMAT_NV21:
    colorFormat = IMoviePlayer::COLOR_NV21;
    break;
  default:
    ASSERT(false, "unknown internal pixel format: %d\n", colorFormat);
    break;
  }

  return colorFormat;
}

// -----------------------------------------------------------------------------
// MoviePlayer
// -----------------------------------------------------------------------------
MoviePlayer::MoviePlayer(InitParam &param)
: mPlayer(nullptr)
, mInitParam(param)
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
}

bool
MoviePlayer::Open(const char *filepath)
{
  mPlayer = new MoviePlayerCore(conv_color_format(mInitParam.videoColorFormat),
                                mInitParam.useOwnAudioEngine);
  return mPlayer->Open(filepath);
}

void
MoviePlayer::Play(bool loop)
{
  LOGV("MoviePlayer: play\n");

  if (mPlayer) {
    return mPlayer->Play(loop);
  }
}

void
MoviePlayer::Stop()
{
  LOGV("MoviePlayer: stop\n");

  if (mPlayer) {
    return mPlayer->Stop();
  }
}

void
MoviePlayer::Pause()
{
  LOGV("MoviePlayer: pause\n");

  if (mPlayer) {
    return mPlayer->Pause();
  }
}

void
MoviePlayer::Resume()
{
  LOGV("MoviePlayer: resume\n");

  if (mPlayer) {
    return mPlayer->Resume();
  }
}

void
MoviePlayer::Seek(int64_t posUs)
{
  LOGV("MoviePlayer: seek posUs=%lld\n", posUs);

  if (mPlayer) {
    return mPlayer->Seek(posUs);
  }
}

void
MoviePlayer::SetLoop(bool loop)
{
  LOGV("MoviePlayer: set loop=%d\n", loop);

  if (mPlayer) {
    return mPlayer->SetLoop(loop);
  }
}

bool
MoviePlayer::IsVideoAvailable() const
{
  if (mPlayer) {
    return mPlayer->IsVideoAvailable();
  } else {
    return false;
  }
}

void
MoviePlayer::GetVideoFormat(VideoFormat *format) const
{
  if (IsVideoAvailable() && format != nullptr) {
    format->width       = mPlayer->Width();
    format->height      = mPlayer->Height();
    format->frameRate   = mPlayer->FrameRate();
    format->colorFormat = conv_pixel_format(mPlayer->OutputPixelFormat());
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
    format->sampleRate    = mPlayer->SampleRate();
    format->channels      = mPlayer->Channels();
    format->bitsPerSample = mPlayer->BitsPerSample();
    int32_t encoding      = mPlayer->Encoding();
    switch (encoding) {
    case AUDIO_FORMAT_S16:
      format->encoding = PCM_S16;
      break;
    case AUDIO_FORMAT_U8:
      format->encoding = PCM_U8;
      break;
    case AUDIO_FORMAT_F32:
      format->encoding = PCM_F32;
      break;
    case AUDIO_FORMAT_S32:
      format->encoding = PCM_S32;
      break;
    default:
      format->encoding = PCM_UNKNOWN;
      ASSERT(false, "unsupported audio encoding: %d", encoding);
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
MoviePlayer::RenderFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                         ColorFormat format)
{
  if (dst == nullptr) {
    LOGV("MoviePlayer: invalid destination buffer.\n");
    return;
  }

  if (!mPlayer) {
    LOGV("MoviePlayer: internal player is not running.\n");
    return;
  }

  if (!IsPlaying()) {
    LOGV("MoviePlayer: video is not playing now.\n");
    return;
  }

  PixelFormat internalFormat = PIXEL_FORMAT_UNKNOWN;
  switch (format) {
  case COLOR_UNKNOWN:
    break;
  case COLOR_ABGR:
    internalFormat = PIXEL_FORMAT_ABGR;
    break;
  case COLOR_ARGB:
    internalFormat = PIXEL_FORMAT_ARGB;
    break;
  case COLOR_RGBA:
    internalFormat = PIXEL_FORMAT_RGBA;
    break;
  case COLOR_BGRA:
    internalFormat = PIXEL_FORMAT_BGRA;
    break;
  case COLOR_I420:
  case COLOR_NV12:
  case COLOR_NV21:
    ASSERT(false, "yuv format not supported for MoviePlayer::RenderFrame().\n");
    break;
  default:
    ASSERT(false, "unknown color format: %d\n", format);
    break;
  }

  const DecodedBuffer *dcBuf = mPlayer->GetDecodedFrame();
  if (dcBuf == nullptr) {
    LOGV("no valid decoded frame exist.\n");
    return;
  }

  if (dcBuf->frame < 0) {
    // 無効フレーム
    LOGV("invalid frame number.\n");
    return;
  }

  // LOGV("decoded buffer frame: %lld size:%zu\n", buf->frame, buf->dataSize);

  // TODO 事前色変換処理での逆ストライド対応
  //      事前にカラーフォーマットだけでなく、RenderFrameの情報を
  //      まるっと設定できるようにしておくか、SetColorFormat()にdibModeみたいなフラグをつけるか
  // 色変換済みの場合はストライドに合わせて詰め直しの処理を行う
  if (strideBytes == w * 4) {
    memcpy(dst, dcBuf->data, dcBuf->dataSize);
  } else {
    uint8_t *d = dst;
    uint8_t *s = dcBuf->data;
    int spitch = w * 4;
    for (int y = 0; y < h; y++) {
      memcpy(d, s, spitch);
      s += spitch;
      d += strideBytes;
    }
  }
}

// TODO WIP
bool
MoviePlayer::GetVideoFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                           uint64_t *timeStampUs)
{
  return true;
}

bool
MoviePlayer::GetAudioFrame(uint8_t *frames, int64_t frameCount, uint64_t *framesRead,
                           uint64_t *timeStampUs)
{
  return true;
}

IMoviePlayer *
IMoviePlayer::CreateMoviePlayer(const char *filename, InitParam &param)
{
  MoviePlayer *player = new MoviePlayer(param);
  if (player->Open(filename)) {
    return player;
  }
  delete player;
  return nullptr;
}

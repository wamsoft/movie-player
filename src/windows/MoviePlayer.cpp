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
  mPlayer = new MoviePlayerCore();
  mPlayer->SetPixelFormat(conv_color_format(mColorFormat));
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

void
MoviePlayer::SetColorFormat(ColorFormat format)
{
  LOGV("MoviePlayer: set ColorFormat=%d\n", format);

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

  // デフォルトカラーフォーマットが未指定なので、ここでピクセル変換を行う
  if (mColorFormat == MoviePlayer::UNKNOWN) {
    // 毎フレーム色変換を行う場合はlibyuv側で変換時に逆strideにも対応した処理を
    // 行ってくれるのでそれに任せる
    convert_yuv_to_rgb32(dst, strideBytes, internalFormat, dcBuf);
  } else {
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

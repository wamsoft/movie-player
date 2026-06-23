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

  switch (pixelFormat) {
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
                                mInitParam.audioSink);
  return mPlayer->Open(filepath);
}

bool
MoviePlayer::Open(IMovieReadStream *stream)
{
  mPlayer = new MoviePlayerCore(conv_color_format(mInitParam.videoColorFormat),
                                mInitParam.audioSink);
  return mPlayer->Open(stream);
}

IMoviePlayer::State 
MoviePlayer::GetState() const
{
  if (mPlayer) {
    return (IMoviePlayer::State)mPlayer->GetState();
  } else {
    return State::STATE_UNINIT;
  }
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
  LOGV("MoviePlayer: seek posUs=%" PRId64 "\n", posUs);

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

void
MoviePlayer::SetVolume(float volume)
{
  if (mPlayer) {
    mPlayer->SetVolume(volume);
  }
}

float
MoviePlayer::Volume() const
{
  float volume = 1.0f;
  if (mPlayer) {
    volume = mPlayer->Volume();
  }
  return volume;
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
MoviePlayer::SetOnState(OnState func, void *userPtr)
{
  if (!mPlayer) {
    LOGE("MoviePlayer: internal player is not running.\n");
  }
  mPlayer->SetOnState([func, userPtr](MoviePlayerCore::State state) {
    func(userPtr, (IMoviePlayer::State)state);
  });
}

// 旧 API: ARGB / RGBA / BGRA 等の packed format 専用の高速経路。
// updater(dest, pitch) を 1 回呼ぶことで packed RGBA を host バッファに直接
// 書き込ませる。 余計な memcpy を経由しない。 YUV を要求した場合の挙動は未定義
// (SetOnVideoDecodedPlanes を使うこと)。
void
MoviePlayer::SetOnVideoDecoded(OnVideoDecoded callback)
{
  if (!mPlayer) {
    LOGE("MoviePlayer: internal player is not running.\n");
  }
  mPlayer->SetOnVideoDecoded([callback](const DecodedBuffer *data) {
    int w = data->v.width;
    int h = data->v.height;
    int spitch = w * 4; // packed RGBA のストライド
    char *src = (char*)data->data;
    callback(w, h, [w, h, spitch, src](char *dest, int dpitch) {
      if (dpitch == spitch && dpitch > 0) {
        memcpy(dest, src, spitch * h);
      } else {
        char *d = dest;
        char *s = src;
        for (int y = 0; y < h; y++) {
          memcpy(d, s, spitch);
          s += spitch;
          d += dpitch;
        }
      }
    });
  });
}

// 新 API: YUV plane 含む全 format 対応。 host は VideoFrameInfo の planes[] を直接読む。
// SetOnVideoDecoded と排他 (内部 slot を上書きするので最後に呼んだ方のみ有効)。
void
MoviePlayer::SetOnVideoDecodedPlanes(OnVideoDecodedPlanes callback)
{
  if (!mPlayer) {
    LOGE("MoviePlayer: internal player is not running.\n");
  }
  mPlayer->SetOnVideoDecoded([callback](const DecodedBuffer *data) {
    VideoFrameInfo info{};
    info.width        = data->v.width;
    info.height       = data->v.height;
    info.colorFormat  = conv_pixel_format(data->v.format);

    int W = data->v.width;
    int H = data->v.height;
    int W2 = (W + 1) / 2;
    int H2 = (H + 1) / 2;

    switch (info.colorFormat) {
    case IMoviePlayer::COLOR_I420:
      info.planeCount = 3;
      info.planes[IMoviePlayer::VIDEO_PLANE_Y] = { data->v.planes[VDB_PLANE_Y], W,  H,  data->v.stride[VDB_PLANE_Y] };
      info.planes[IMoviePlayer::VIDEO_PLANE_U] = { data->v.planes[VDB_PLANE_U], W2, H2, data->v.stride[VDB_PLANE_U] };
      info.planes[IMoviePlayer::VIDEO_PLANE_V] = { data->v.planes[VDB_PLANE_V], W2, H2, data->v.stride[VDB_PLANE_V] };
      break;
    case IMoviePlayer::COLOR_NV12:
    case IMoviePlayer::COLOR_NV21:
      info.planeCount = 2;
      info.planes[IMoviePlayer::VIDEO_PLANE_Y] = { data->v.planes[VDB_PLANE_Y], W,  H,  data->v.stride[VDB_PLANE_Y] };
      info.planes[1]                           = { data->v.planes[VDB_PLANE_U], W2, H2, data->v.stride[VDB_PLANE_U] };
      break;
    case IMoviePlayer::COLOR_ARGB:
    case IMoviePlayer::COLOR_ABGR:
    case IMoviePlayer::COLOR_RGBA:
    case IMoviePlayer::COLOR_BGRA:
    default:
      info.planeCount = 1;
      info.planes[IMoviePlayer::VIDEO_PLANE_PACKED] = {
        data->v.planes[VDB_PLANE_PACKED] ? data->v.planes[VDB_PLANE_PACKED] : (const uint8_t*)data->data,
        W, H,
        data->v.stride[VDB_PLANE_PACKED] > 0 ? data->v.stride[VDB_PLANE_PACKED] : (W * 4)
      };
      break;
    }
    callback(info);
  });
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

IMoviePlayer *
IMoviePlayer::CreateMoviePlayer(IMovieReadStream *stream, InitParam &param)
{
  MoviePlayer *player = new MoviePlayer(param);
  if (player->Open(stream)) {
    return player;
  }
  delete player;
  return nullptr;
}

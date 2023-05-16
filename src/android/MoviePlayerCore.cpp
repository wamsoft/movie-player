#define MYLOG_TAG "MoviePlayerCore"
#include "BasicLog.h"

#include "Constants.h"
#include "CommonUtils.h"
#include "MediaTimer.h"
#include "VideoTrackPlayer.h"
#include "AudioTrackPlayer.h"
#include "MoviePlayerCore.h"
#include "PixelConvert.h"

#include <unistd.h>

#include "media/NdkMediaCrypto.h"
#include "media/NdkMediaCodec.h"
#include "media/NdkMediaError.h"
#include "media/NdkMediaFormat.h"
#include "media/NdkMediaExtractor.h"
#include "media/NdkMediaDataSource.h"

// -----------------------------------------------------------------------------
// MoviePlayerCore
// -----------------------------------------------------------------------------
MoviePlayerCore::MoviePlayerCore()
: mVideoTrackPlayer(nullptr)
, mAudioTrackPlayer(nullptr)
, mFd(-1)
, mIsLoop(false)
, mOnAudioDecoded(nullptr)
, mOnAudioDecodedUserPtr(nullptr)
{
  Init();
}

MoviePlayerCore::~MoviePlayerCore()
{
  Done();
}

void
MoviePlayerCore::Init()
{
  mIsLoop = false;

  mPixelFormat = PIXEL_FORMAT_UNKNOWN;

  mOnAudioDecoded        = nullptr;
  mOnAudioDecodedUserPtr = nullptr;
}

void
MoviePlayerCore::Done()
{
  if (mAudioTrackPlayer != nullptr) {
    mAudioTrackPlayer->Stop();
    delete mAudioTrackPlayer;
    mAudioTrackPlayer = nullptr;
  }

  if (mVideoTrackPlayer != nullptr) {
    mVideoTrackPlayer->Stop();
    delete mVideoTrackPlayer;
    mVideoTrackPlayer = nullptr;
  }

  if (mFd >= 0) {
    ::close(mFd);
    mFd = -1;
  }
}

void
MoviePlayerCore::Start()
{
  if (mAudioTrackPlayer) {
    mAudioTrackPlayer->Start();
  }
  if (mVideoTrackPlayer) {
    mVideoTrackPlayer->Start();
  }
}

bool
MoviePlayerCore::Open(const char *filepath)
{
  LOGV("open file: %s", filepath);

  AMediaExtractor *vEx = CreateExtractor(filepath);
  AMediaExtractor *aEx = CreateExtractor(filepath);
  bool isVideoFound    = vEx && SetupVideoTrackPlayer(vEx);
  bool isAudioFound    = aEx && SetupAudioTrackPlayer(aEx);
  if (isVideoFound || isAudioFound) {
    Start();
    return true;
  }
  return false;
}

bool
MoviePlayerCore::Open(int fd, off_t offset, off_t length)
{
  if (fd >= 0) {
    mFd                  = fd;
    AMediaExtractor *vEx = CreateExtractor(dup(fd), offset, length);
    AMediaExtractor *aEx = CreateExtractor(dup(fd), offset, length);
    bool isVideoFound    = vEx && SetupVideoTrackPlayer(vEx);
    bool isAudioFound    = aEx && SetupAudioTrackPlayer(aEx);
    if (isVideoFound || isAudioFound) {
      Start();
      return true;
    }
  }
  return false;
}

AMediaExtractor *
MoviePlayerCore::CreateExtractor(const char *filepath)
{
  AMediaExtractor *ex = AMediaExtractor_new();
  media_status_t err  = AMediaExtractor_setDataSource(ex, filepath);
  if (err != AMEDIA_OK) {
    LOGE("setDataSource error: %d", err);
    AMediaExtractor_delete(ex);
    return nullptr;
  }
  return ex;
}

AMediaExtractor *
MoviePlayerCore::CreateExtractor(int fd, off_t offset, off_t length)
{
  AMediaExtractor *ex = AMediaExtractor_new();
  media_status_t err  = AMediaExtractor_setDataSourceFd(ex, fd, offset, length);
  if (err != AMEDIA_OK) {
    LOGE("setDataSource error: %d", err);
    AMediaExtractor_delete(ex);
    return nullptr;
  }
  return ex;
}

bool
MoviePlayerCore::SetupVideoTrackPlayer(AMediaExtractor *ex)
{
  // 初めて見つけたビデオトラックのみを対象にする
  int numtracks = AMediaExtractor_getTrackCount(ex);
  for (int i = 0; i < numtracks; i++) {
    AMediaFormat *format = AMediaExtractor_getTrackFormat(ex, i);
    const char *s        = AMediaFormat_toString(format);
    const char *mime;
    if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
      LOGV("no mime type");
      AMediaFormat_delete(format);
      AMediaExtractor_delete(ex);
      return false;
    } else if (!strncmp(mime, "video/", 6)) {
      mVideoTrackPlayer = new VideoTrackPlayer(ex, i, &mTimer);
      AMediaFormat_delete(format);
      return true;
    } else {
      AMediaFormat_delete(format);
    }
  }

  AMediaExtractor_delete(ex);
  return false;
}

bool
MoviePlayerCore::SetupAudioTrackPlayer(AMediaExtractor *ex)
{
  // 初めて見つけたオーディオトラックのみを対象にする
  int numtracks = AMediaExtractor_getTrackCount(ex);
  for (int i = 0; i < numtracks; i++) {
    AMediaFormat *format = AMediaExtractor_getTrackFormat(ex, i);
    const char *s        = AMediaFormat_toString(format);
    const char *mime;
    if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
      LOGV("no mime type");
      AMediaFormat_delete(format);
      AMediaExtractor_delete(ex);
      return false;
    } else if (!strncmp(mime, "audio/", 6)) {
      mAudioTrackPlayer = new AudioTrackPlayer(ex, i, &mTimer);
      AMediaFormat_delete(format);
      // 流れ的にはOpen()した後にオーディオフォーマットを確認してからなので
      // まだ未設定のはずだが一応ハンドラ設定を確認
      if (mOnAudioDecoded != nullptr) {
        mAudioTrackPlayer->SetOnAudioDecoded(mOnAudioDecoded, mOnAudioDecodedUserPtr);
      }

      return true;
    } else {
      AMediaFormat_delete(format);
    }
  }

  AMediaExtractor_delete(ex);
  return false;
}

void
MoviePlayerCore::SendTrackControl(int track, int32_t msg, int64_t arg, void *data)
{
  switch (track) {
  case TRACK_VIDEO:
    if (mVideoTrackPlayer != nullptr) {
      mVideoTrackPlayer->Post(msg, arg, data);
    }
    break;
  case TRACK_AUDIO:
    if (mAudioTrackPlayer != nullptr) {
      mAudioTrackPlayer->Post(msg, arg, data);
    }
    break;
  default:
    ASSERT(false, "unknown track type: %d", track);
    break;
  }
}

void
MoviePlayerCore::WaitTrackEvent(int track, int32_t event, int64_t timeoutUs)
{
  switch (track) {
  case TRACK_VIDEO:
    if (mVideoTrackPlayer != nullptr) {
      mVideoTrackPlayer->WaitEvent(event, timeoutUs);
    }
    break;
  case TRACK_AUDIO:
    if (mAudioTrackPlayer != nullptr) {
      mAudioTrackPlayer->WaitEvent(event, timeoutUs);
    }
    break;
  default:
    ASSERT(false, "unknown track type: %d", track);
    break;
  }
}

void
MoviePlayerCore::Play(bool loop)
{
  LOGV("play");

  SetLoop(loop);

  SendTrackControl(TRACK_AUDIO, AudioTrackPlayer::MSG_START);
  SendTrackControl(TRACK_VIDEO, VideoTrackPlayer::MSG_START);

  WaitTrackEvent(TRACK_AUDIO, AudioTrackPlayer::EVENT_FLAG_PLAY_READY);
  WaitTrackEvent(TRACK_VIDEO, VideoTrackPlayer::EVENT_FLAG_PLAY_READY);
}

void
MoviePlayerCore::Stop()
{
  LOGV("stop");

  SendTrackControl(TRACK_AUDIO, AudioTrackPlayer::MSG_STOP);
  SendTrackControl(TRACK_VIDEO, VideoTrackPlayer::MSG_STOP);

  WaitTrackEvent(TRACK_AUDIO, AudioTrackPlayer::EVENT_FLAG_STOPPED);
  WaitTrackEvent(TRACK_VIDEO, VideoTrackPlayer::EVENT_FLAG_STOPPED);
}

void
MoviePlayerCore::Pause()
{
  LOGV("pause");

  SendTrackControl(TRACK_AUDIO, AudioTrackPlayer::MSG_PAUSE);
  SendTrackControl(TRACK_VIDEO, VideoTrackPlayer::MSG_PAUSE);
}

void
MoviePlayerCore::Resume()
{
  LOGV("resume");

  SendTrackControl(TRACK_AUDIO, AudioTrackPlayer::MSG_RESUME);
  SendTrackControl(TRACK_VIDEO, VideoTrackPlayer::MSG_RESUME);
}

void
MoviePlayerCore::Seek(int64_t posUs)
{
  LOGV("seek posUs=%lld", posUs);

  SendTrackControl(TRACK_AUDIO, AudioTrackPlayer::MSG_SEEK, posUs);
  SendTrackControl(TRACK_VIDEO, VideoTrackPlayer::MSG_SEEK, posUs);
}

void
MoviePlayerCore::SetLoop(bool loop)
{
  LOGV("set loop=%d", loop);

  mIsLoop = loop;
  SendTrackControl(TRACK_AUDIO, AudioTrackPlayer::MSG_SET_LOOP, loop);
  SendTrackControl(TRACK_VIDEO, VideoTrackPlayer::MSG_SET_LOOP, loop);
}

void
MoviePlayerCore::SetPixelFormat(PixelFormat format)
{
  mPixelFormat = format;
}

int32_t
MoviePlayerCore::Width() const
{
  if (mVideoTrackPlayer) {
    return mVideoTrackPlayer->Width();
  } else {
    return -1;
  }
}

int32_t
MoviePlayerCore::Height() const
{
  if (mVideoTrackPlayer) {
    return mVideoTrackPlayer->Height();
  } else {
    return -1;
  }
}

bool
MoviePlayerCore::IsAudioAvailable() const
{
  return (mAudioTrackPlayer != nullptr && mAudioTrackPlayer->IsValid());
}

int32_t
MoviePlayerCore::SampleRate() const
{
  if (mAudioTrackPlayer != nullptr) {
    return mAudioTrackPlayer->SampleRate();
  } else {
    return -1;
  }
}

int32_t
MoviePlayerCore::Channels() const
{
  if (mAudioTrackPlayer != nullptr) {
    return mAudioTrackPlayer->Channels();
  } else {
    return -1;
  }
}

int32_t
MoviePlayerCore::BitsPerSample() const
{
  if (mAudioTrackPlayer != nullptr) {
    return mAudioTrackPlayer->BitsPerSample();
  } else {
    return -1;
  }
}

int32_t
MoviePlayerCore::Encoding() const
{
  if (mAudioTrackPlayer != nullptr) {
    return mAudioTrackPlayer->Encoding();
  } else {
    return -1;
  }
}

int64_t
MoviePlayerCore::Duration() const
{
  return mTimer.GetDuration();
}

int64_t
MoviePlayerCore::Position() const
{
  return mTimer.GetCurrentMediaTime();
}

bool
MoviePlayerCore::IsPlaying() const
{
  bool isVideoPlaying = mVideoTrackPlayer && mVideoTrackPlayer->IsPlaying();
  bool isAudioPlaying = mAudioTrackPlayer && mAudioTrackPlayer->IsPlaying();
  return (isVideoPlaying || isAudioPlaying);
}

bool
MoviePlayerCore::Loop() const
{
  return mIsLoop;
}

void
MoviePlayerCore::SetOnAudioDecoded(OnAudioDecoded func, void *userPtr)
{
  mOnAudioDecoded        = func;
  mOnAudioDecodedUserPtr = userPtr;
  if (mAudioTrackPlayer) {
    mAudioTrackPlayer->SetOnAudioDecoded(func, userPtr);
  }
}

void
MoviePlayerCore::RenderFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                             PixelFormat format)
{
  if (dst == nullptr) {
    LOGE("invalid destination buffer.");
    return;
  }

  if (!mVideoTrackPlayer) {
    LOGE("video track player is not running.");
    return;
  }

  if (!mVideoTrackPlayer->IsPlaying()) {
    LOGV("video track is not playing.");
    return;
  }

  // TODO 固定フォーマット指定対応

  // 現状ではAndroid版では毎回yuv>rgb変換を行っていて、
  // RenderFrame()で渡されたstrideをそのままlibyuvに通せる
  // libyuv側で逆stride処理に対応しているのでテンポラリバッファ等使わずそのまま通す
  DecodedFrame *frame = mVideoTrackPlayer->GetDecodedFrame();
  if (frame) {
    std::lock_guard<std::mutex> lock(frame->dataMutex);
    if (frame->IsValid()) {

      // TODO
      // YUVのプレーンごとのストライドなどを取得する方法が見当たらないので
      // ソースプレーンがパディングされずに連続して配置されるものとして扱っている
      // 一部デバイスや特殊サイズムービーだとうまくいかない可能性はある
      const uint8_t *yBuf = frame->data;
      const uint8_t *uBuf = yBuf + frame->width * frame->height;
      const uint8_t *vBuf = uBuf + (frame->width * frame->height / 4);
      int32_t yStride     = frame->width;
      int32_t uvStride    = frame->width / 2;
      if (is_nv_pixel_format(frame->colorFormat)) {
        uvStride = frame->width; // NVはUVがパックド
      }
      convert_yuv_to_rgb32(dst, strideBytes, format, yBuf, uBuf, vBuf, 0, yStride, uvStride, 0,
                           w, h, frame->colorFormat, frame->colorSpace,
                           frame->colorRange);
    }
  }
}

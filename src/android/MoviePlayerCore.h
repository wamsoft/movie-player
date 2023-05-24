#pragma once

#include "VideoTrackPlayer.h"
#include "AudioTrackPlayer.h"
#include "MediaClock.h"

#include "media/NdkMediaExtractor.h"

// ムービープレイヤー内部実装クラス
class MoviePlayerCore
{
public:
  MoviePlayerCore();
  virtual ~MoviePlayerCore();

  bool Open(const char *filepath);
  bool Open(int fd, off_t offset, off_t length);

  void Start();

  void Play(bool loop = false);
  void Stop();
  void Pause();
  void Resume();
  void Seek(int64_t posUs);
  void SetLoop(bool loop);

  void SetPixelFormat(PixelFormat format);

  int32_t Width() const;
  int32_t Height() const;

  bool IsAudioAvailable() const;
  int32_t SampleRate() const;
  int32_t Channels() const;
  int32_t BitsPerSample() const;
  int32_t Encoding() const;

  int64_t Duration() const;
  int64_t Position() const;
  bool IsPlaying() const;
  bool Loop() const;

  void RenderFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                   PixelFormat format);

  void SetOnAudioDecoded(OnAudioDecoded func, void *userPtr);

private:
  void Init();
  void Done();

  AMediaExtractor *CreateExtractor(const char *filepath);
  AMediaExtractor *CreateExtractor(int fd, off_t offset, off_t length);

  bool SetupVideoTrackPlayer(AMediaExtractor *ex);
  bool SetupAudioTrackPlayer(AMediaExtractor *ex);

  enum
  {
    TRACK_VIDEO = 0,
    TRACK_AUDIO,
  };
  void SendTrackControl(int track, int32_t msg, int64_t arg = 0, void *data = nullptr);
  void WaitTrackEvent(int track, int32_t event, int64_t timeoutUs = 0);

private:
  VideoTrackPlayer *mVideoTrackPlayer;
  AudioTrackPlayer *mAudioTrackPlayer;

  MediaClock mClock;

  int mFd;

  bool mIsLoop;

  // video 情報
  PixelFormat mPixelFormat;

  // audio 情報
  OnAudioDecoded mOnAudioDecoded;
  void *mOnAudioDecodedUserPtr;
};
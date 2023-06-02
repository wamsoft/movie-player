#pragma once

#include "Constants.h"
#include "MessageLooper.h"
#include "WebmExtractor.h"
#include "Decoder.h"
#include "MediaClock.h"
#include "IMoviePlayer.h"

class AudioEngine;

class MoviePlayerCore : public MessageLooper
{
public:
  enum Message
  {
    MSG_NOP,
    MSG_PRELOAD,
    MSG_START,
    MSG_DECODE,
    MSG_PAUSE,
    MSG_RESUME,
    MSG_SET_LOOP,
    MSG_SEEK,
    MSG_STOP,
  };

  enum State
  {
    STATE_UNINIT,
    STATE_OPEN,
    STATE_PRELOADING,
    STATE_PLAY,
    STATE_PAUSE,
    STATE_STOP,
    STATE_FINISH,
  };

public:
  MoviePlayerCore(PixelFormat pixelFormat, bool useAudioEngine);
  virtual ~MoviePlayerCore();

  void Init();
  void Done();

  bool Open(const char *filepath);
  void Play(bool loop = false);
  void Stop();
  void Pause();
  void Resume();
  void Seek(int64_t posUs);
  void SetLoop(bool loop);

  bool IsVideoAvailable() const;
  // width/heightはvideo trackをExtractorでselect後でないと値が入らないので注意
  // MoviePlayer的にはOpen()で必ずスキャン＋SelectTrackするので
  // 「Open後に正しい情報が取れる」という仕様には反さないので問題にはならないはず。
  int32_t Width() const;
  int32_t Height() const;
  float FrameRate() const;
  PixelFormat OutputPixelFormat() const;

  bool IsAudioAvailable() const;
  int32_t SampleRate() const;
  int32_t Channels() const;
  int32_t BitsPerSample() const;
  int32_t Encoding() const;
  void SetVolume(float volume);
  float Volume() const;

  int64_t Duration() const;
  int64_t Position() const;
  bool IsPlaying() const;
  bool Loop() const;

  bool GetVideoFrame(const DecodedBuffer **videoFrame);
  bool GetAudioFrame(uint8_t *frames, int64_t frameCount, uint64_t *framesRead,
                     uint64_t *timeStampUs);

  // 入力をプリロードする
  void PreLoadInput();

protected:
  virtual void HandleMessage(int32_t what, int64_t arg, void *data) override;

  void InitStatusFlags();
  void SelectTargetTrack();
  void Start();
  void Decode();
  void DemuxInput();
  int32_t InputToDecoder(Decoder *decoder, bool inputIsEOS);
  void HandleVideoOutput();
  void HandleAudioOutput();
  void Flush();

  void SetState(State newState);
  State GetState() const;
  bool IsCurrentState(State state) const;

  void InitDummyFrame();
  void UpdateVideoFrameToNext();
  void SetVideoFrame(DecodedBuffer *newFrame);
  void SetVideoFrameNext(DecodedBuffer *nextFrame);
  int64_t CalcDiffVideoTimeAndNow(DecodedBuffer *targetFrame) const;

  void EnqueueAudio(DecodedBuffer *buf);

private:
  // ステート
  State mState;
  bool mIsLoop;

  WebmExtractor *mExtractor;
  VideoDecoder *mVideoDecoder;
  AudioDecoder *mAudioDecoder;

  // API用mutex
  mutable std::mutex mApiMutex;

  // video 情報
  int32_t mWidth, mHeight;
  float mFrameRate;
  PixelFormat mOutputPixelFormat;
  PixelFormat mPixelFormat;

  // ストリームフラグ
  // bool mSawInputEOS;
  // bool mSawOutputEOS;

  bool mSawVideoInputEOS, mSawAudioInputEOS;
  bool mSawVideoOutputEOS, mSawAudioOutputEOS;

  bool mLastVideoFrameEnd;
  bool mLastAudioFrameEnd;

  // メディアタイマー
  MediaClock mClock;

  // デコード済みフレーム
  mutable std::mutex mVideoFrameMutex;
  DecodedBuffer mDummyFrame;
  DecodedBuffer *mVideoFrame, *mVideoFrameNext;
  DecodedBuffer *mVideoFrameLastGet;

  // オーディオ
  bool mUseAudioEngine;
  AudioEngine *mAudioEngine;
  std::mutex mAudioFrameMutex;
  size_t mAudioQueuedBytes;
  size_t mAudioDataPos;
  int32_t mAudioLastBufIndex;
  int32_t mAudioUnitSize;
  int64_t mAudioOutputFrames;
  uint64_t mAudioCodecDelayUs;
  struct
  {
    uint64_t base;         // 現在のフレーム先頭のPTS
    uint64_t outputFrames; // 現在のPTSを持つデータから何個出力したか
                           // (同PTSが複数パケットにわたるケースがあるのでその対応用)
    void Reset() { base = INT64_MAX, outputFrames = 0; }
  } mAudioTime;
  std::queue<DecodedBuffer *> mAudioFrameQueue;

  // 同期用イベントフラグ
  enum
  {
    EVENT_FLAG_PRELOADED  = 1 << 0,
    EVENT_FLAG_PLAY_READY = 1 << 1,
    EVENT_FLAG_STOPPED    = 1 << 2,
  };
  EventFlag mEventFlag;
};
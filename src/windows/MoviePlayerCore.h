#pragma once

#include "Constants.h"
#include "MessageLooper.h"
#include "WebmExtractor.h"
#include "Decoder.h"
#include "MediaTimer.h"

class AudioEngine;

class MoviePlayerCore : public MessageLooper
{
  // TODO test
  friend class AudioEngine;
  
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
  MoviePlayerCore();
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

  void SetPixelFormat(PixelFormat format);

  bool IsVideoAvailable() const;
  // width/heightはvideo trackをExtractorでselect後でないと値が入らないので注意
  // MoviePlayer的にはOpen()で必ずスキャン＋SelectTrackするので
  // 「Open後に正しい情報が取れる」という仕様には反さないので問題にはならないはず。
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

  // TODO A/V 考慮(現状はVideoデータ決め打ち対応)
  const DecodedBuffer *GetDecodedFrame() const;

  bool GetAudio(uint8_t *frames, uint64_t frameCount, uint64_t *framesRead);

  // 入力をプリロードする
  void PreLoadInput();

protected:
  virtual void HandleMessage(int32_t what, int64_t arg, void *data) override;

  void SelectTargetTrack();
  void Start();
  void Decode(bool oneShot = false);
  void HandleOutput(bool isPreloading);
  void HandleInput(bool isPreloading);

  void SetState(State newState);
  State GetState() const;
  void InitDummyFrame();
  void UpdateDecodedFrame(DecodedBuffer *newFrame);
  void UpdateDecodedFrameNext(DecodedBuffer *nextFrame);

  // TODO Audio対応したので、Video決め打ちになってる名前(UpdateDecodedFrameとか)を
  //      一通りどうするか見直すこと

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
  PixelFormat mPixelFormat;

  // ストリームフラグ
  bool mSawInputEOS;
  bool mSawOutputEOS;

  // メディアタイマー
  MediaTimer mTimer;

  // デコード済みフレーム
  mutable std::mutex mDecodedFrameMutex;
  DecodedBuffer mDummyFrame;
  DecodedBuffer *mDecodedFrame, *mDecodedFrameNext;

  // オーディオ
  AudioEngine *mAudioEngine;

  // 同期用イベントフラグ
  enum
  {
    EVENT_FLAG_PRELOADED  = 1 << 0,
    EVENT_FLAG_PLAY_READY = 1 << 1,
    EVENT_FLAG_STOPPED    = 1 << 2,
  };
  EventFlag mEventFlag;
};
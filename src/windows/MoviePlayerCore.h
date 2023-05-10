#pragma once

#include "Constants.h"
#include "MessageLooper.h"
#include "WebmExtractor.h"
#include "Decoder.h"
#include "MediaTimer.h"

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

  // width/heightはvideo trackをExtractorでselect後でないと値が入らないので注意
  // MoviePlayer的にはOpen()で必ずスキャン＋SelectTrackするので
  // 「Open後に正しい情報が取れる」という仕様には反さないので問題にはならないはず。
  int32_t Width() const;
  int32_t Height() const;
  int64_t Duration() const;
  int64_t Position() const;
  bool IsPlaying() const;
  bool Loop() const;

  // TODO A/V 考慮(現状はVideoデータ決め打ち対応)
  const DecodedBuffer *GetDecodedFrame() const;

  // 入力をプリロードする
  void PreLoadInput();

protected:
  virtual void HandleMessage(int32_t what, int64_t arg, void *data) override;

  void SelectTargetTrack();
  void Start();
  void Decode(bool oneShot = false);
  void SetState(State newState);
  State GetState() const;
  void InitDummyFrame();
  void UpdateDecodedFrame(DecodedBuffer *newFrame);

private:
  // ステート
  State mState;
  bool mIsLoop;

  WebmExtractor *mExtractor;
  Decoder *mVideoDecoder;
  Decoder *mAudioDecoder;

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
  DecodedBuffer *mDecodedFrame;

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
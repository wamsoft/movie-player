#pragma once

#include "Constants.h"
#include "MessageLooper.h"
#include "WebmExtractor.h"
#include "Decoder.h"
#include "MediaClock.h"
#include <functional>

class IAudioSink;
class IMovieReadStream;

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
    MSG_FINISH
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
  MoviePlayerCore(PixelFormat pixelFormat, IAudioSink *audioSink);
  virtual ~MoviePlayerCore();

  void Init();
  void Done();

  bool Open(const char *filepath);
  bool Open(IMovieReadStream *stream);

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

  void SetOnState(std::function<void(State)> func) {
    mOnStateFunc = func;
  }

  void SetOnVideoDecoded(std::function<void(const DecodedBuffer *)> func) {
    mOnVideoDecodedFunc = func;
  }

  // 入力をプリロードする
  void PreLoadInput();

  State GetState() const;

protected:
  virtual void HandleMessage(int32_t what, int64_t arg, void *data) override;

  void OpenSetup();
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
  bool IsCurrentState(State state) const;

  void InitDummyFrame();
  void UpdateVideoFrameToNext();
  void SetVideoFrame(DecodedBuffer *newFrame);
  void SetVideoFrameNext(DecodedBuffer *nextFrame);
  int64_t CalcDiffVideoTimeAndNow(DecodedBuffer *targetFrame) const;

  void EnqueueAudio(DecodedBuffer *buf);
  void EnqueueVideo(DecodedBuffer *buf);

  // sink->TryPopConsumed を drain して decoder buffer を release し、
  // 同時に sink->GetSamplesPlayed から MediaClock を更新する。
  void DrainAudioSinkConsumed();

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

  // IN/OUTステータスフラグ
  bool mSawVideoInputEOS, mSawAudioInputEOS;
  bool mSawVideoOutputEOS, mSawAudioOutputEOS;
  bool mLastVideoFrameEnd;
  bool mLastAudioFrameEnd;

  // メディアクロック
  MediaClock mClock;

  // 出力ビデオフレーム
  mutable std::mutex mVideoFrameMutex;
  DecodedBuffer mDummyFrame;
  DecodedBuffer *mVideoFrame, *mVideoFrameNext;
  DecodedBuffer *mVideoFrameLastGet;

  // 外部 audio sink (host が用意)。所有しない。nullptr なら audio 無し再生。
  IAudioSink *mAudioSink;

  // 出力オーディオフレーム情報
  int32_t mAudioUnitSize;       // オーディオ1サンプルのサイズ
  uint64_t mAudioCodecDelayUs;  // audio codec delay (出力しない頭のオフセット)
  uint64_t mAudioStartPtsNs;    // 最初に enqueue した audio buffer の PTS
                                // (sink->GetSamplesPlayed と組合せて clock を計算)
  bool mAudioStartPtsValid;     // mAudioStartPtsNs が有効かどうか
  int64_t mAudioResumeMediaTimeUs; // resume 時に最速反映するための最終出力時刻

  std::function<void(State)> mOnStateFunc;
  std::function<void(const DecodedBuffer *)> mOnVideoDecodedFunc;

  // 同期用イベントフラグ
  enum
  {
    EVENT_FLAG_PRELOADED  = 1 << 0,
    EVENT_FLAG_PLAY_READY = 1 << 1,
    EVENT_FLAG_STOPPED    = 1 << 2,
  };
  EventFlag mEventFlag;
};
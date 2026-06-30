#pragma once

#include "TrackPlayer.h"

class IAudioSink;

// -----------------------------------------------------------------------------
// オーディオトラックプレイヤ
//   AMediaCodec が出力した PCM を host 提供の IAudioSink へ流すだけ。
//   再生 / mix / device 出力 / clock anchor 取得は sink 側で行う。
// -----------------------------------------------------------------------------
class AudioTrackPlayer : public TrackPlayer
{
public:
  AudioTrackPlayer(AMediaExtractor *ex, int32_t trackIndex, MediaClock *timer,
                   IAudioSink *audioSink);
  virtual ~AudioTrackPlayer();

  void Init();
  void Done();

  virtual void HandleOutputData(ssize_t bufIdx, AMediaCodecBufferInfo &bufInfo,
                                int flags) override;

  int32_t SampleRate() const;
  int32_t Channels() const;
  int32_t BitsPerSample() const;
  int32_t Encoding() const;
  int32_t MaxInputSize() const;

  // audio sink が Setup に成功して active かどうか
  bool HasActiveSink() const { return mAudioSink != nullptr; }

private:
  virtual void HandleMessage(int32_t what, int64_t arg, void *data) override;

  // 実デコード出力フォーマットが確定したあと (最初の出力バッファ時点) に
  // sample rate / channels / encoding を読み直して sink を Setup する。
  // コンストラクタ (AMediaCodec_start 直後) では Opus 等で誤レートを掴むため。
  void EnsureSinkSetup();

  // sink->TryPopConsumed を drain して AMediaCodec_releaseOutputBuffer を呼ぶ。
  // (audio callback コンテキストで release を直接呼ばずこちら (decoder thread)
  //  で肩代わりするための仕組み)
  void DrainAudioSinkConsumed();

  // mAudioSink->GetSamplesPlayed() を起点 PTS と組合せて
  // mClock.UpdateAnchorTime() を叩く (audio-master clock 駆動)。
  void UpdateClockFromSink();

  // sink->GetSamplesPlayed() は累積カウンタなので、PTS 起点が変わる度に
  // 当時の累積を baseline として保存し、以降の差分でメディア時間を進める。
  void InvalidateStartPts();

private:
  // output 情報
  int32_t mChannels;
  int32_t mSampleRate;
  int32_t mBitsPerSample;
  int32_t mEncoding;
  int32_t mMaxInputSize;

  // clock anchor 用の起点 PTS / sample baseline。
  // mStartPtsValid == false の間は次に enqueue したバッファの PTS と
  // その時点の sink->GetSamplesPlayed() を起点として記録する。
  bool    mStartPtsValid;
  int64_t mStartPtsUs;
  int64_t mSamplesPlayedBase;

  // 出力フォーマット確定後の sink 遅延セットアップを一度だけ行うためのフラグ
  bool    mSinkSetup;

  // host 提供の audio sink (所有しない)
  IAudioSink *mAudioSink;
};

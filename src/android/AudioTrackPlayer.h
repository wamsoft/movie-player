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

private:
  virtual void HandleMessage(int32_t what, int64_t arg, void *data) override;

  // sink->TryPopConsumed を drain して AMediaCodec_releaseOutputBuffer を呼ぶ。
  // (audio callback コンテキストで release を直接呼ばずこちら (decoder thread)
  //  で肩代わりするための仕組み)
  void DrainAudioSinkConsumed();

private:
  // output 情報
  int32_t mChannels;
  int32_t mSampleRate;
  int32_t mBitsPerSample;
  int32_t mEncoding;
  int32_t mMaxInputSize;

  // host 提供の audio sink (所有しない)
  IAudioSink *mAudioSink;
};

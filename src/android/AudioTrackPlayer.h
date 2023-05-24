#pragma once

#include "TrackPlayer.h"

// -----------------------------------------------------------------------------
// オーディオトラックプレイヤ
// -----------------------------------------------------------------------------

// オーディオデコーダコールバック
typedef int32_t (*OnAudioDecoded)(void *userPtr, uint8_t *data, size_t sizeBytes);

class AudioTrackPlayer : public TrackPlayer
{
public:
  AudioTrackPlayer(AMediaExtractor *ex, int32_t trackIndex, MediaClock *timer);
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

  void SetOnAudioDecoded(OnAudioDecoded func, void *userPtr);

private:
  virtual void HandleMessage(int32_t what, int64_t arg, void *data) override;

private:
  // output 情報
  int32_t mChannels;
  int32_t mSampleRate;
  int32_t mBitsPerSample;
  int32_t mEncoding;
  int32_t mMaxInputSize;

  // オーディオコールバック
  OnAudioDecoded mOnAudioDecoded;
  void *mOnAudioDecodedUserPtr;
};
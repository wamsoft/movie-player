#pragma once

#include "TrackPlayer.h"
#include <mutex>

// -----------------------------------------------------------------------------
// オーディオトラックプレイヤ
// -----------------------------------------------------------------------------

// オーディオデコーダコールバック
typedef int32_t (*OnAudioDecoded)(void *userPtr, const uint8_t *data, size_t sizeBytes);

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

  // リングバッファから指定フレーム数を読み出す
  bool ReadFromRingBuffer(uint8_t* buffer, uint64_t frameCount, uint64_t* framesRead);

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

  // リングバッファ関連
  uint8_t* mRingBuffer;
  size_t mRingBufferSize;
  size_t mRingBufferWritePos;
  size_t mRingBufferReadPos;
  size_t mRingBufferDataSize;
  std::mutex mRingBufferMutex;

  // リングバッファ初期化
  void InitRingBuffer();
  // リングバッファへのデータ書き込み
  void WriteToRingBuffer(uint8_t* data, size_t size);
};
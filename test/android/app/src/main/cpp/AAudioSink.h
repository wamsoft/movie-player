// -----------------------------------------------------------------------------
// AAudioSink
//   movie-player のテスト用最小 IAudioSink 実装 (Android / AAudio)。
//   minSdk 28 を前提に AAudio (API 26+) をそのまま使う。外部ライブラリ依存なし。
//
//   ・Setup() で AAudioStream を作る (low-latency 指定)。
//   ・Enqueue() で受け取ったバッファを内部 pending キューに積み、
//     AAudio のデータコールバック内で吸い出して出力する。
//     バッファ消費完了の通知 (param) は consumed キュー経由で
//     TryPopConsumed() から返す。
//   ・GetSamplesPlayed() は AAudioStream_getTimestamp の position を使う。
//     これは「デバイスから実際に再生されたフレーム数」を返すので、
//     audio-master 同期のための anchor として使える。
//     取得できない場合は内部の書き込みフレーム数を返す (近似)。
//
//   コールバックはリアルタイムスレッドで動くため、ロック取得は最小限に。
// -----------------------------------------------------------------------------
#pragma once

#include "IAudioSink.h"

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>

class AAudioSink : public IAudioSink
{
public:
  AAudioSink() = default;

  ~AAudioSink() override
  {
    if (mStream) {
      AAudioStream_requestStop(mStream);
      AAudioStream_close(mStream);
      mStream = nullptr;
    }
  }

  bool Setup(int channels, int sampleRate, int bitsPerSample, Encoding encoding) override
  {
    if (mStream) return false;

    aaudio_format_t fmt = AAUDIO_FORMAT_PCM_I16;
    switch (encoding) {
    case PCM_U8:
      // AAudio に PCM_U8 は無い。テストでは S16 にフォールバックして変換しないで
      // 鳴らすが、本来は呼び出し側で変換するべき。
      return false;
    case PCM_S16: fmt = AAUDIO_FORMAT_PCM_I16; break;
    case PCM_S32: fmt = AAUDIO_FORMAT_PCM_I32; break;
    case PCM_F32: fmt = AAUDIO_FORMAT_PCM_FLOAT; break;
    default: return false;
    }

    mChannels      = channels;
    mSampleRate    = sampleRate;
    mBitsPerSample = bitsPerSample;
    mEncoding      = encoding;
    mFrameBytes    = (channels * bitsPerSample) / 8;
    if (mFrameBytes <= 0) return false;

    AAudioStreamBuilder *builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || !builder) {
      return false;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setFormat(builder, fmt);
    AAudioStreamBuilder_setDataCallback(builder, &AAudioSink::DataCallback, this);

    aaudio_result_t r = AAudioStreamBuilder_openStream(builder, &mStream);
    AAudioStreamBuilder_delete(builder);
    if (r != AAUDIO_OK || !mStream) {
      mStream = nullptr;
      return false;
    }
    return true;
  }

  void Enqueue(const void *data, size_t bytes, bool last, void *param) override
  {
    if (!mStream || !data || bytes == 0) {
      if (param) {
        std::lock_guard<std::mutex> lk(mLock);
        mConsumed.push_back(param);
      }
      return;
    }
    std::lock_guard<std::mutex> lk(mLock);
    mPending.push_back(Buf{ (const uint8_t *)data, bytes, 0, param, last });
  }

  void Start() override
  {
    if (mStream) {
      AAudioStream_requestStart(mStream);
    }
  }

  void Stop() override
  {
    if (mStream) {
      AAudioStream_requestPause(mStream);
    }
  }

  int64_t GetSamplesPlayed() const override
  {
    if (!mStream) return 0;
    int64_t framePos = 0;
    int64_t timeNs   = 0;
    aaudio_result_t r =
      AAudioStream_getTimestamp(const_cast<AAudioStream *>(mStream), CLOCK_MONOTONIC,
                                &framePos, &timeNs);
    if (r == AAUDIO_OK && framePos >= 0) {
      return framePos;
    }
    // タイムスタンプが取れない初期段階などは書き込み済み frames を返す
    return mFramesWritten.load(std::memory_order_acquire);
  }

  bool TryPopConsumed(void **outParam) override
  {
    std::lock_guard<std::mutex> lk(mLock);
    if (mConsumed.empty()) return false;
    void *p = mConsumed.front();
    mConsumed.pop_front();
    if (outParam) *outParam = p;
    return true;
  }

  void Flush() override
  {
    if (mStream) {
      // pending を空にして consumed へ移送
      std::lock_guard<std::mutex> lk(mLock);
      for (auto &b : mPending) {
        mConsumed.push_back(b.param);
      }
      mPending.clear();
    }
  }

  void SetVolume(float volume) override
  {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    mVolume.store(volume, std::memory_order_release);
  }

  float Volume() const override { return mVolume.load(std::memory_order_acquire); }

private:
  struct Buf
  {
    const uint8_t *data;
    size_t bytes;
    size_t offset;
    void *param;
    bool last;
  };

  static aaudio_data_callback_result_t DataCallback(AAudioStream * /*stream*/,
                                                    void *userData, void *audioData,
                                                    int32_t numFrames)
  {
    auto *self = reinterpret_cast<AAudioSink *>(userData);
    return self->OnData(audioData, numFrames);
  }

  aaudio_data_callback_result_t OnData(void *audioData, int32_t numFrames)
  {
    size_t needBytes = (size_t)numFrames * mFrameBytes;
    uint8_t *dst     = reinterpret_cast<uint8_t *>(audioData);
    size_t written   = 0;

    // pending から吸い出し、終わったものは consumed へ
    {
      std::lock_guard<std::mutex> lk(mLock);
      while (written < needBytes && !mPending.empty()) {
        Buf &b      = mPending.front();
        size_t left = b.bytes - b.offset;
        size_t copy = (left < (needBytes - written)) ? left : (needBytes - written);
        std::memcpy(dst + written, b.data + b.offset, copy);
        written  += copy;
        b.offset += copy;
        if (b.offset >= b.bytes) {
          mConsumed.push_back(b.param);
          mPending.pop_front();
        }
      }
    }

    // 不足分は無音 (underrun を切らずに継続)
    if (written < needBytes) {
      std::memset(dst + written, 0, needBytes - written);
    }

    // volume 適用 (簡易、PCM_S16 / S32 / F32 のみサポート)
    float vol = mVolume.load(std::memory_order_acquire);
    if (vol < 0.999f) {
      ApplyVolume(audioData, numFrames, vol);
    }

    mFramesWritten.fetch_add(numFrames, std::memory_order_release);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }

  void ApplyVolume(void *audioData, int32_t numFrames, float vol)
  {
    int32_t totalSamples = numFrames * mChannels;
    switch (mEncoding) {
    case PCM_S16: {
      auto *p = reinterpret_cast<int16_t *>(audioData);
      for (int i = 0; i < totalSamples; ++i) p[i] = (int16_t)(p[i] * vol);
      break;
    }
    case PCM_S32: {
      auto *p = reinterpret_cast<int32_t *>(audioData);
      for (int i = 0; i < totalSamples; ++i) p[i] = (int32_t)(p[i] * vol);
      break;
    }
    case PCM_F32: {
      auto *p = reinterpret_cast<float *>(audioData);
      for (int i = 0; i < totalSamples; ++i) p[i] = p[i] * vol;
      break;
    }
    default:
      break;
    }
  }

private:
  AAudioStream    *mStream         = nullptr;
  int              mChannels       = 0;
  int              mSampleRate     = 0;
  int              mBitsPerSample  = 0;
  Encoding         mEncoding       = PCM_S16;
  int              mFrameBytes     = 0;
  std::atomic<float>   mVolume{ 1.0f };
  std::atomic<int64_t> mFramesWritten{ 0 };

  std::mutex          mLock;
  std::deque<Buf>     mPending;  // 未再生バッファ
  std::deque<void *>  mConsumed; // 再生完了 (param のみ)
};

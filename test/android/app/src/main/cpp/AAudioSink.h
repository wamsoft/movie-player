// -----------------------------------------------------------------------------
// AAudioSink
//   movie-player のテスト用最小 IAudioSink 実装 (Android / AAudio)。
//   minSdk 28 を前提に AAudio (API 26+) をそのまま使う。外部ライブラリ依存なし。
//
//   ・Setup() で AAudioStream を作る。
//     - PerformanceMode は NONE。LOW_LATENCY はミキサ (および内蔵リサンプラ)
//       を bypass してデバイス native rate に強制ロックされるため、コーデック
//       出力レートと食い違うとテンポずれの原因になる。
//     - open 後に AAudioStream_get* で実 rate/channels/format を取得して、
//       要求と一致するか検証する。channels/format は AAudio 側で変換され
//       ないので不一致なら Setup を fail させて audio 無し再生に倒す。
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
#include <android/log.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

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
    // PerformanceMode は NONE (= AAudioMixer 経由)。
    // LOW_LATENCY を使うとミキサがバイパスされ device native rate に
    // ロックされるため、コーデック出力レートと食い違ったときにテンポずれが
    // 発生する (Vorbis 44100Hz を 48000Hz native 端末で鳴らすと遅くなる等)。
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
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

    // AAudio は要求値を「ヒント」としか扱わないので、open 後に実値を取得
    // して検証する。
    //   - channels / format は AAudio 側で自動変換されない。一致しなければ
    //     Setup を諦めて audio 無し再生に倒す。
    //   - sample rate は NONE モードならミキサが SRC してくれる想定だが、
    //     一致しないときはログを出して把握可能にしておく。
    int32_t actualRate     = AAudioStream_getSampleRate(mStream);
    int32_t actualChannels = AAudioStream_getChannelCount(mStream);
    aaudio_format_t actualFmt = AAudioStream_getFormat(mStream);

    __android_log_print(ANDROID_LOG_INFO, "AAudioSink",
                        "open: req rate=%d ch=%d fmt=%d / actual rate=%d ch=%d fmt=%d",
                        sampleRate, channels, (int)fmt,
                        actualRate, actualChannels, (int)actualFmt);

    if (actualChannels != channels || actualFmt != fmt) {
      __android_log_print(ANDROID_LOG_ERROR, "AAudioSink",
                          "channels/format mismatch -- giving up audio");
      AAudioStream_close(mStream);
      mStream = nullptr;
      return false;
    }
    if (actualRate != sampleRate) {
      __android_log_print(ANDROID_LOG_WARN, "AAudioSink",
                          "sample rate mismatch (req=%d actual=%d) -- "
                          "relying on AAudio mixer SRC",
                          sampleRate, actualRate);
    }
    // OnData は出力バッファの大きさを numFrames * mFrameBytes として計算する
    // ので、必ず *実 channels* で計算する。
    mFrameBytes = (actualChannels * bitsPerSample) / 8;

    // 参考情報として burst / buffer 容量も出す。underrun 解析のヒントになる。
    int32_t framesPerBurst = AAudioStream_getFramesPerBurst(mStream);
    int32_t bufCapacity    = AAudioStream_getBufferCapacityInFrames(mStream);
    __android_log_print(ANDROID_LOG_INFO, "AAudioSink",
                        "stream: framesPerBurst=%d bufferCapacity=%d frameBytes=%d",
                        framesPerBurst, bufCapacity, mFrameBytes);
    return true;
  }

  // 受け取った PCM は内部に *copy* したうえで、すぐ consumed として返す。
  // - data ポインタは codec output buffer (借り物) なので、再生完了まで保持
  //   していると codec output slot が pinned のままになり、codec の input 側
  //   までバックプレッシャがかかってしまう (audio decoder iteration が
  //   AMediaCodec_dequeueInputBuffer の 10ms 待ちに陥り、結果として
  //   mPending が空のまま OnData が無音パディングで埋める = 体感「音だけ
  //   遅い」になる)。
  // - copy 1 回ぶんの memcpy コスト (1 audio frame ≒ 数 KB / 数十 ms) より、
  //   codec を free に保つメリットが大きい。
  void Enqueue(const void *data, size_t bytes, bool last, void *param) override
  {
    if (!mStream || !data || bytes == 0) {
      if (param) {
        std::lock_guard<std::mutex> lk(mLock);
        mConsumed.push_back(param);
      }
      return;
    }
    Buf b;
    b.data.assign(static_cast<const uint8_t *>(data),
                  static_cast<const uint8_t *>(data) + bytes);
    b.offset = 0;
    b.last   = last;
    {
      std::lock_guard<std::mutex> lk(mLock);
      mPending.push_back(std::move(b));
      if (param) {
        // codec slot は呼び出し側で即 release できるよう、コピー直後に
        // consumed 通知へ載せる。
        mConsumed.push_back(param);
      }
    }
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
      // Enqueue 時点で param は consumed へ流し済み。
      // ここでは pending を捨てるだけで OK。
      std::lock_guard<std::mutex> lk(mLock);
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
  // Enqueue でコピーした PCM を保持する (codec output buffer は借りたまま
  // にしないので param は持たない)。
  struct Buf
  {
    std::vector<uint8_t> data;
    size_t offset = 0;
    bool   last   = false;
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

    {
      std::lock_guard<std::mutex> lk(mLock);
      while (written < needBytes && !mPending.empty()) {
        Buf &b      = mPending.front();
        size_t left = b.data.size() - b.offset;
        size_t copy = (left < (needBytes - written)) ? left : (needBytes - written);
        std::memcpy(dst + written, b.data.data() + b.offset, copy);
        written  += copy;
        b.offset += copy;
        if (b.offset >= b.data.size()) {
          mPending.pop_front();
        }
      }
    }

    // 不足分は無音 (underrun を切らずに継続)
    if (written < needBytes) {
      std::memset(dst + written, 0, needBytes - written);
      // underrun 集計 (1 callback で足りなかったら +1)。500 callback ごとに
      // logcat に吐く (~5-10 秒間隔程度のはず)。
      uint64_t cnt = mUnderrunCount.fetch_add(1, std::memory_order_relaxed) + 1;
      uint64_t calls = mCallbackCount.load(std::memory_order_relaxed);
      if ((calls % 500) == 0 && calls > 0) {
        __android_log_print(ANDROID_LOG_WARN, "AAudioSink",
                            "underrun: %llu / %llu callbacks",
                            (unsigned long long)cnt,
                            (unsigned long long)calls);
      }
    }
    mCallbackCount.fetch_add(1, std::memory_order_relaxed);

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
  std::atomic<uint64_t> mCallbackCount{ 0 };
  std::atomic<uint64_t> mUnderrunCount{ 0 };

  std::mutex          mLock;
  std::deque<Buf>     mPending;  // 未再生バッファ (PCM コピー所有)
  std::deque<void *>  mConsumed; // codec slot 解放用の param キュー
};

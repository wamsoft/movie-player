#pragma once

#include <cstdint>
#include <cstddef>

// -----------------------------------------------------------------------------
// IAudioSink
//   movie-player の外側 (host) が用意する音声出力先の抽象。
//   movie-player は内部に audio engine を持たず、デコード済み PCM を
//   この sink に流し込むだけ。再生・mix・device 出力・MediaClock のための
//   再生位置取得はすべて host の責任。
// -----------------------------------------------------------------------------
class IAudioSink
{
public:
  enum Encoding
  {
    PCM_U8,
    PCM_S16,
    PCM_S32,
    PCM_F32,
  };

  virtual ~IAudioSink() {}

  // 音声フォーマット確定通知。Open 時に 1 回だけ呼ばれる。
  // 戻り値 false で audio output を断念 (movie-player は audio 無しで再生継続)。
  virtual bool Setup(int channels, int sampleRate, int bitsPerSample,
                     Encoding encoding) = 0;

  // PCM データの投入。data ポインタは sink が consumed 通知を返すまで
  // 寿命を保つ責任が enqueue 側にある (= movie-player 側の DecodedBuffer 寿命)。
  // last=true は EOS 表現 (この後新規データは来ない)。
  // param は consumed 通知時に返ってくる識別子。
  virtual void Enqueue(const void *data, size_t bytes, bool last,
                       void *param) = 0;

  // 出力開始/停止 (state 変化に追従)
  virtual void Start() = 0;
  virtual void Stop() = 0;

  // 再生済み累積フレーム数 (sample granule、device clock ベース)。
  // MediaClock の anchor source として使用される。
  virtual int64_t GetSamplesPlayed() const = 0;

  // 再生完了したエントリの param を 1 件取り出す (なければ false)。
  // 呼び出し側 (movie-player decoder thread) は受け取った param を
  // DecodedBuffer* として release する想定。
  virtual bool TryPopConsumed(void **outParam) = 0;

  // pending を全て consumed に流す (Seek/Flush 用)。
  virtual void Flush() = 0;

  // 音量 (0.0 - 1.0)
  virtual void SetVolume(float volume) = 0;
  virtual float Volume() const = 0;
};

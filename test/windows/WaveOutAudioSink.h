// -----------------------------------------------------------------------------
// WaveOutAudioSink
//   movie-player のテスト用最小 IAudioSink 実装 (Windows / WaveOut)。
//   winmm.lib のみで動き、外部依存無し。
//
//   ・Setup() で WaveOut デバイスを開く
//   ・Enqueue() で渡されたバッファを waveOutWrite。所有権は呼び出し側のままで、
//     WOM_DONE コールバックで Consumed キューへ移し TryPopConsumed() で返す。
//   ・GetSamplesPlayed() は waveOutGetPosition(TIME_SAMPLES) を直に返す
//     (デバイス側の累積再生サンプル数。Stop 中は進まない)
//
//   テスト用なので、エラー処理は最低限。
// -----------------------------------------------------------------------------
#pragma once

#include "IAudioSink.h"

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>

#pragma comment(lib, "winmm.lib")

class WaveOutAudioSink : public IAudioSink
{
public:
  WaveOutAudioSink() = default;

  ~WaveOutAudioSink() override
  {
    if (mWaveOut) {
      // 未完了バッファを全部 WHDR_DONE 化させてから close
      waveOutReset(mWaveOut);
      DrainConsumedLocked(); // 自前で残りを掃除
      waveOutClose(mWaveOut);
      mWaveOut = nullptr;
    }
  }

  bool Setup(int channels, int sampleRate, int bitsPerSample, Encoding encoding) override
  {
    if (mWaveOut) return false; // 二重 Setup 禁止

    mChannels      = channels;
    mSampleRate    = sampleRate;
    mBitsPerSample = bitsPerSample;
    mEncoding      = encoding;

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = (encoding == PCM_F32) ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    wfx.nChannels       = (WORD)channels;
    wfx.nSamplesPerSec  = (DWORD)sampleRate;
    wfx.wBitsPerSample  = (WORD)bitsPerSample;
    wfx.nBlockAlign     = (WORD)((channels * bitsPerSample) / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize          = 0;

    MMRESULT mr = waveOutOpen(&mWaveOut, WAVE_MAPPER, &wfx,
                              (DWORD_PTR)&WaveOutAudioSink::WaveOutProc,
                              (DWORD_PTR)this, CALLBACK_FUNCTION);
    if (mr != MMSYSERR_NOERROR) {
      mWaveOut = nullptr;
      return false;
    }

    ApplyVolumeLocked();

    // open 直後はデフォルトで再生中扱い。Start() で waveOutRestart を呼ぶことで
    // pause からの再開も統一して扱える。
    return true;
  }

  void Enqueue(const void *data, size_t bytes, bool last, void *param) override
  {
    // 空 / 失敗系は即座に consumed キューへ。WAVEHDR は dwFlags=0 のままで
    // TryPopConsumed 側の waveOutUnprepareHeader は no-op として通る。
    auto pushFailed = [this, param]() {
      auto *fake     = new WAVEHDR{};
      fake->dwUser   = (DWORD_PTR)param;
      fake->dwFlags  = 0;
      std::lock_guard<std::mutex> lk(mLock);
      mConsumed.push_back(fake);
    };

    if (!mWaveOut || !data || bytes == 0) {
      pushFailed();
      return;
    }

    auto *hdr = new WAVEHDR{};
    hdr->lpData          = const_cast<LPSTR>(reinterpret_cast<LPCSTR>(data));
    hdr->dwBufferLength  = (DWORD)bytes;
    hdr->dwBytesRecorded = 0;
    hdr->dwUser          = (DWORD_PTR)param;
    hdr->dwFlags         = 0;
    hdr->dwLoops         = 0;

    MMRESULT mr = waveOutPrepareHeader(mWaveOut, hdr, sizeof(*hdr));
    if (mr != MMSYSERR_NOERROR) {
      delete hdr;
      pushFailed();
      return;
    }

    mr = waveOutWrite(mWaveOut, hdr, sizeof(*hdr));
    if (mr != MMSYSERR_NOERROR) {
      waveOutUnprepareHeader(mWaveOut, hdr, sizeof(*hdr));
      delete hdr;
      pushFailed();
      return;
    }

    (void)last; // EOS マーカーは特段の扱い不要 (バッファ枯渇で自然に止まる)
  }

  void Start() override
  {
    if (mWaveOut) waveOutRestart(mWaveOut);
  }

  void Stop() override
  {
    if (mWaveOut) waveOutPause(mWaveOut);
  }

  int64_t GetSamplesPlayed() const override
  {
    if (!mWaveOut) return 0;
    MMTIME mmt = {};
    mmt.wType = TIME_SAMPLES;
    waveOutGetPosition(const_cast<HWAVEOUT>(mWaveOut), &mmt, sizeof(mmt));
    // TIME_SAMPLES の単位は (チャンネル合計でなく) フレーム数として返るのが
    // 一般的だが、ドライバによっては TIME_BYTES にフォールバックされる場合が
    // あるので戻り型を確認する。
    if (mmt.wType == TIME_SAMPLES) {
      return (int64_t)mmt.u.sample;
    } else if (mmt.wType == TIME_BYTES && mBitsPerSample > 0 && mChannels > 0) {
      int64_t frameBytes = (int64_t)mChannels * mBitsPerSample / 8;
      if (frameBytes > 0) return (int64_t)mmt.u.cb / frameBytes;
    } else if (mmt.wType == TIME_MS && mSampleRate > 0) {
      return (int64_t)mmt.u.ms * mSampleRate / 1000;
    }
    return 0;
  }

  bool TryPopConsumed(void **outParam) override
  {
    WAVEHDR *hdr = nullptr;
    {
      std::lock_guard<std::mutex> lk(mLock);
      if (mConsumed.empty()) return false;
      hdr = mConsumed.front();
      mConsumed.pop_front();
    }
    void *param = (void *)hdr->dwUser;
    // WaveOutProc 内で waveOutUnprepareHeader を呼ぶのは MSDN 的に避ける
    // 必要があるので、consumer thread (= TryPopConsumed の呼び出し元) で
    // 後始末する。
    waveOutUnprepareHeader(mWaveOut, hdr, sizeof(*hdr));
    delete hdr;
    if (outParam) *outParam = param;
    return true;
  }

  void Flush() override
  {
    if (!mWaveOut) return;
    // waveOutReset で全 pending を WHDR_DONE 化 → WaveOutProc 経由で mConsumed
    // に積まれる。呼び出し側 (movie-player) は次の TryPopConsumed ループで
    // 回収する想定。
    waveOutReset(mWaveOut);
  }

  void SetVolume(float volume) override
  {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    mVolume = volume;
    ApplyVolumeLocked();
  }

  float Volume() const override { return mVolume; }

private:
  static void CALLBACK WaveOutProc(HWAVEOUT, UINT uMsg, DWORD_PTR dwInst,
                                   DWORD_PTR p1, DWORD_PTR /*p2*/)
  {
    if (uMsg != WOM_DONE) return;
    auto *self = reinterpret_cast<WaveOutAudioSink *>(dwInst);
    auto *hdr  = reinterpret_cast<WAVEHDR *>(p1);
    if (!self || !hdr) return;
    self->OnBufferDone(hdr);
  }

  void OnBufferDone(WAVEHDR *hdr)
  {
    // WaveOutProc 内では unprepare せずに WAVEHDR 自体を consumed キューに
    // 積み、TryPopConsumed 側で後始末する。
    std::lock_guard<std::mutex> lk(mLock);
    mConsumed.push_back(hdr);
  }

  void ApplyVolumeLocked()
  {
    if (!mWaveOut) return;
    DWORD v16 = (DWORD)(mVolume * 0xFFFF);
    if (v16 > 0xFFFF) v16 = 0xFFFF;
    DWORD packed = (v16 & 0xFFFF) | ((v16 & 0xFFFF) << 16); // L | R
    waveOutSetVolume(mWaveOut, packed);
  }

  void DrainConsumedLocked()
  {
    // dtor 用。waveOutReset 後、callback で積まれた残りを正しく後始末する。
    std::lock_guard<std::mutex> lk(mLock);
    for (WAVEHDR *hdr : mConsumed) {
      if (hdr) {
        if (mWaveOut) waveOutUnprepareHeader(mWaveOut, hdr, sizeof(*hdr));
        delete hdr;
      }
    }
    mConsumed.clear();
  }

private:
  HWAVEOUT mWaveOut       = nullptr;
  int      mChannels      = 0;
  int      mSampleRate    = 0;
  int      mBitsPerSample = 0;
  Encoding mEncoding      = PCM_S16;
  float    mVolume        = 1.0f;

  std::mutex            mLock;
  std::deque<WAVEHDR *> mConsumed; // 再生完了 WAVEHDR (TryPopConsumed で unprepare + delete)
};

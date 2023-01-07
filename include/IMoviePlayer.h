#pragma once

#include <cstdint>
#include <cstdio>
#include <sys/types.h>

// オーディオデコーダコールバック
typedef int32_t (*OnAudioDecoded)(void *userPtr, uint8_t *data, size_t sizeBytes);

class IMoviePlayer
{
public:
  // RenderFrame用カラーフォーマット定数
  // 並びはバイト列順なのでLEでは ARGB=0xBBGGRRAA
  // OpenGLやDXGI(DX10～)と互換でDX9と逆
  // libyuvはAPI名がxxxToARGBの場合はLEで0xBBGGRRAA
  // 参考: IMoviePlayer::BGRA の場合
  //  IMoviePlayer::ColorFormatと同じ並び
  //    OpenGL: GL_BGRA
  //    DXGI:   DXGI_FORMAT_B8G8R8A8_UNORM
  //  IMoviePlayer::ColorFormatと逆の並び
  //    DX9:    D3DFMT_A8R8G8B8
  //    libyuv: ARGB
  enum ColorFormat
  {
    UNKNOWN = -1,
    ARGB    = 0,
    ABGR    = 1,
    RGBA    = 2,
    BGRA    = 3,
    I420    = 10,
    NV12    = 11,
    NV21    = 12,
  };

  // オーディオコールバックで得られるデータの形式
  enum PcmEncoding
  {
    PCM_UNKNOWN = -1,
    PCM_8       = 0,
    PCM_16      = 1,
    PCM_32      = 2,
    PCM_FLOAT   = 3,
  };

  // オーディオ出力フォーマット
  struct AudioFormat
  {
    int32_t sampleRate;
    int32_t channels;
    int32_t bitsPerSample;
    PcmEncoding encoding;
    int32_t maxInputSize; // max input buffersize
  };

  IMoviePlayer()
  : mColorFormat(UNKNOWN)
  , mOnAudioDecoded(nullptr)
  , mOnAudioDecodedUserPtr(nullptr)
  {}
  virtual ~IMoviePlayer() {}

  virtual void Play(bool loop = false)                  = 0;
  virtual void Stop()                                   = 0;
  virtual void Pause()                                  = 0;
  virtual void Resume()                                 = 0;
  virtual void Seek(int64_t posUs)                      = 0;
  virtual void SetLoop(bool loop)                       = 0;

  // RenderFrameで渡されるColorFormatではなく固定のフォーマットとして設定する
  // Open()前に呼ぶ必要があり、Open後の呼び出しは作用が保証されない。
  virtual void SetColorFormat(ColorFormat format) { mColorFormat = format; }

  // video info
  virtual int32_t Width() const  = 0;
  virtual int32_t Height() const = 0;

  // audio info
  virtual bool IsAudioAvailable() const { return false; }
  virtual void GetAudioFormat(AudioFormat *format) const
  {
    if (format) {
      format->sampleRate    = -1;
      format->channels      = -1;
      format->bitsPerSample = -1;
      format->encoding      = PCM_UNKNOWN;
      format->maxInputSize  = -1;
    }
  }

  // info
  virtual int64_t Duration() const = 0;
  virtual int64_t Position() const = 0;
  virtual bool IsPlaying() const   = 0;
  virtual bool Loop() const        = 0;

  // SetColorFormat()で固定フォーマットを指定した場合はformatの値は無視される
  virtual void RenderFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                           ColorFormat format = UNKNOWN) = 0;

  // Audioコールバック
  virtual void SetOnAudioDecoded(OnAudioDecoded func, void *userPtr)
  {
    mOnAudioDecoded        = func;
    mOnAudioDecodedUserPtr = userPtr;
  }

  static IMoviePlayer* CreateMoviePlayer(const char *filename);

protected:
  ColorFormat mColorFormat;
  OnAudioDecoded mOnAudioDecoded;
  void *mOnAudioDecodedUserPtr;
};

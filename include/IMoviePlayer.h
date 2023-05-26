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
    COLOR_UNKNOWN = -1,
    COLOR_NOCONV  = COLOR_UNKNOWN, // デコーダ出力を無変換で出す
    COLOR_ARGB    = 0,
    COLOR_ABGR    = 1,
    COLOR_RGBA    = 2,
    COLOR_BGRA    = 3,
    COLOR_I420    = 10,
    COLOR_NV12    = 11,
    COLOR_NV21    = 12,
  };

  // オーディオデータの形式
  enum PcmEncoding
  {
    PCM_UNKNOWN = -1,
    PCM_U8      = 0,
    PCM_S16     = 1,
    PCM_S32     = 2,
    PCM_F32     = 3,
  };

  // オーディオ出力フォーマット
  struct AudioFormat
  {
    int32_t sampleRate;
    int32_t channels;
    int32_t bitsPerSample;
    PcmEncoding encoding;
  };

  IMoviePlayer()
  : mColorFormat(COLOR_UNKNOWN)
  , mOnAudioDecoded(nullptr)
  , mOnAudioDecodedUserPtr(nullptr)
  {}
  virtual ~IMoviePlayer() {}

  virtual void Play(bool loop = false) = 0;
  virtual void Stop()                  = 0;
  virtual void Pause()                 = 0;
  virtual void Resume()                = 0;
  virtual void Seek(int64_t posUs)     = 0;
  virtual void SetLoop(bool loop)      = 0;

  // TODO Create時にパラメータとして渡すように変更してこれは削除する

  // RenderFrameで渡されるColorFormatではなく固定のフォーマットとして設定する
  // Open()前に呼ぶ必要があり、Open後の呼び出しは作用が保証されない。
  virtual void SetColorFormat(ColorFormat format) { mColorFormat = format; }

  // video info
  virtual bool IsVideoAvailable() const = 0;
  virtual int32_t Width() const         = 0;
  virtual int32_t Height() const        = 0;
  virtual float FrameRate() const       = 0;

  // audio info
  virtual bool IsAudioAvailable() const                  = 0;
  virtual void GetAudioFormat(AudioFormat *format) const = 0;

  // info
  virtual int64_t Duration() const = 0;
  virtual int64_t Position() const = 0;
  virtual bool IsPlaying() const   = 0;
  virtual bool Loop() const        = 0;

  // TODO Audioとの絡みで名称変更予定
  // SetColorFormat()で固定フォーマットを指定した場合はformatの値は無視される
  virtual void RenderFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                           ColorFormat format = COLOR_UNKNOWN) = 0;

  // TODO こちらのインタフェースに変更予定
  //      - 前回から更新があればtrueが返る
  //      - フレームごとのカラーフォーマット指定はおそらく無駄なので廃止
  virtual bool GetVideoFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                             uint64_t *timeStampUs)
  {
    return false;
  }
  virtual bool GetAudioFrame(uint8_t *frames, int64_t frameCount, uint64_t *framesRead,
                             uint64_t *timeStampUs)
  {
    return false;
  }

  static IMoviePlayer *CreateMoviePlayer(const char *filename,
                                         ColorFormat format = COLOR_UNKNOWN);

protected:
  ColorFormat mColorFormat;
  OnAudioDecoded mOnAudioDecoded;
  void *mOnAudioDecodedUserPtr;
};

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

  // ビデオ出力フォーマット
  struct VideoFormat
  {
    int32_t width;
    int32_t height;
    float frameRate;
    ColorFormat colorFormat;
  };

  // オーディオ出力フォーマット
  struct AudioFormat
  {
    int32_t sampleRate;
    int32_t channels;
    int32_t bitsPerSample;
    PcmEncoding encoding;
  };

  enum ColorRange
  {
    COLOR_RANGE_UNDEF = 0,
    COLOR_RANGE_LIMITED,
    COLOR_RANGE_FULL,
  };

  enum ColorSpace
  {
    COLOR_SPACE_UNKNOWN  = -1,
    COLOR_SPACE_IDENTITY = 0,
    COLOR_SPACE_BT_601,
    COLOR_SPACE_BT_709,
    COLOR_SPACE_SMPTE_170,
    COLOR_SPACE_SMPTE_240,
    COLOR_SPACE_BT_2020,
    COLOR_SPACE_SRGB,
  };

  enum VideoPlaneIndex
  {
    VIDEO_PLANE_PACKED = 0,
    VIDEO_PLANE_Y      = 0,
    VIDEO_PLANE_U      = 1,
    VIDEO_PLANE_V      = 2,
    VIDEO_PLANE_A      = 3,
    VIDEO_PLANE_COUNT
  };

  // ビデオフレーム
  // *dataの内容は、次のGetVideoFrame()呼び出しでtrueが返る
  // (VideoFrameが更新される)まで有効なので、必要なら適宜コピー保持すること
  struct VideoFrame
  {
    ColorFormat colorFormat;

    // カラースペース、カラーレンジはYUVフォーマット時のみ有効
    // InitParam::videoColorFormatでRGB変換を指定している場合は
    // 変換前のYUV状態でのcs/crが格納されている
    ColorSpace colorSpace;
    ColorRange colorRange;

    int32_t width;
    int32_t height;
    int32_t displayWidth;
    int32_t displayHeight;

    size_t dataSize;
    uint8_t *data;
    uint8_t *planes[VIDEO_PLANE_COUNT]; // *data 内の各プレーン先頭ポインタ
    int32_t stride[VIDEO_PLANE_COUNT];  // 各プレーンのストライド
  };

  // 生成パラメータ
  struct InitParam
  {
    ColorFormat videoColorFormat;
    bool useOwnAudioEngine;
    void Init()
    {
      videoColorFormat  = COLOR_UNKNOWN;
      useOwnAudioEngine = true;
    }
  };

  IMoviePlayer() {}
  virtual ~IMoviePlayer() {}

  virtual void Play(bool loop = false) = 0;
  virtual void Stop()                  = 0;
  virtual void Pause()                 = 0;
  virtual void Resume()                = 0;
  virtual void Seek(int64_t posUs)     = 0;
  virtual void SetLoop(bool loop)      = 0;

  // video info
  virtual bool IsVideoAvailable() const                  = 0;
  virtual void GetVideoFormat(VideoFormat *format) const = 0;

  // audio info
  virtual bool IsAudioAvailable() const                  = 0;
  virtual void GetAudioFormat(AudioFormat *format) const = 0;

  // audio volume
  virtual void SetVolume(float volume) = 0;
  virtual float Volume() const         = 0;

  // info
  virtual int64_t Duration() const = 0;
  virtual int64_t Position() const = 0;
  virtual bool IsPlaying() const   = 0;
  virtual bool Loop() const        = 0;

  // 出力ビデオフレームを指定したバッファへコピー取得する
  // RGB系カラーフォマットしか想定されていないので、InitParam::videoColorFormatで
  // RGB系フォーマットが指定されていない場合は常に失敗する
  virtual bool GetVideoFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                             uint64_t *timeStampUs = nullptr) = 0;

  // 出力ビデオフレームをプレイヤーの内部バッファを参照する形で取得する
  // デコーダのYUV出力を無変換で取得したい場合などに使用する
  // フレームの詳細なパラメータはVideoFrame構造体のメンバに含まれ
  // フレームデータ自体は、次回GetVideoFrame()がtrueを返す(==更新が発生する)まで有効
  // フレームデータに更新がない場合はfalseを返す
  virtual bool GetVideoFrame(VideoFrame *frame, uint64_t *timeStampUs = nullptr) = 0;

  // 出力オーディオ列を取得する。要求量のデコード出力が溜まっていない場合はfalseを返す。
  // InitParam::useOwnAudioEngineがtrueの場合は、内部AudioEngine側にデータが
  // 吸い上げられている状態で、外部には回せないようになっているので常にfalseを返す
  virtual bool GetAudioFrame(uint8_t *frames, int64_t frameCount, uint64_t *framesRead,
                             uint64_t *timeStampUs = nullptr) = 0;

  static IMoviePlayer *CreateMoviePlayer(const char *filename, InitParam &param);
};

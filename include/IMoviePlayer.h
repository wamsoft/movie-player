#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>

class IAudioSink;

class IMovieReadStream {
public:
    virtual int AddRef(void) = 0;
    virtual int Release(void) = 0;
    virtual size_t Read(void *buf, size_t size) = 0;
    virtual int64_t Tell() const = 0;
    virtual void Seek(int64_t offset, int origin) = 0;
    virtual size_t Size() const = 0;
};


class IMoviePlayer
{
public:
  // Color format constants for RenderFrame
  // The order is by byte sequence, so in LE it's ARGB=0xBBGGRRAA
  // Compatible with OpenGL and DXGI (DX10+), inverse of DX9
  // In libyuv, if API name is xxxToARGB, it's 0xBBGGRRAA in LE
  // Reference: In case of IMoviePlayer::BGRA
  //  Same order as IMoviePlayer::ColorFormat
  //    OpenGL: GL_BGRA
  //    DXGI:   DXGI_FORMAT_B8G8R8A8_UNORM
  //  Inverse order of IMoviePlayer::ColorFormat
  //    DX9:    D3DFMT_A8R8G8B8
  //    libyuv: ARGB
  enum ColorFormat
  {
    COLOR_UNKNOWN = -1,
    COLOR_NOCONV  = COLOR_UNKNOWN, // Output decoder output without conversion
    COLOR_ARGB    = 0,
    COLOR_ABGR    = 1,
    COLOR_RGBA    = 2,
    COLOR_BGRA    = 3,
    COLOR_I420    = 10,
    COLOR_NV12    = 11,
    COLOR_NV21    = 12,
  };

  // Audio data format
  enum PcmEncoding
  {
    PCM_UNKNOWN = -1,
    PCM_U8      = 0,
    PCM_S16     = 1,
    PCM_S32     = 2,
    PCM_F32     = 3,
  };

  // Video output format
  struct VideoFormat
  {
    int32_t width;
    int32_t height;
    float frameRate;
    ColorFormat colorFormat;
  };

  // Audio output format
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

  enum State
  {
    STATE_UNINIT,
    STATE_OPEN,
    STATE_PRELOADING,
    STATE_PLAY,
    STATE_PAUSE,
    STATE_STOP, // Stopped
    STATE_FINISH, // Playback finished
  };

  // Creation parameters
  struct InitParam
  {
    ColorFormat videoColorFormat;
    // audio 出力先。host が用意して渡す。nullptr の場合は audio 無しで再生。
    IAudioSink *audioSink;
    void Init()
    {
      videoColorFormat = COLOR_UNKNOWN;
      audioSink        = nullptr;
    }
  };

  IMoviePlayer() {}
  virtual ~IMoviePlayer() {}

  virtual State GetState() const = 0;

  typedef int32_t (*OnState)(void *userPtr, State state);

  virtual void SetOnState(OnState func, void *userPtr) = 0;

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

  // Video decoder callback (旧型・ARGB 系専用、 高速経路)。
  //   host は updater(dest, pitch) を 1 回呼ぶことで、 decoder 側の packed RGBA バッファを
  //   直接 host バッファに「書き込ませる」 ── 余計な memcpy を経由しない最速ルート。
  //   videoColorFormat = COLOR_ARGB / ABGR / RGBA / BGRA のときに使う。 YUV 形式
  //   (COLOR_I420 / NV12 / NV21) を要求した場合の挙動は未定義 (= plane 経路を使うこと)。
  typedef std::function<void(char *dest, int pitch)> DestUpdater;
  typedef std::function<void(int w, int h, DestUpdater updater)> OnVideoDecoded;
  virtual void SetOnVideoDecoded(OnVideoDecoded callback) = 0;

  // Video decoder callback (新型・YUV plane 対応、 汎用経路)。
  //
  // host は VideoFrameInfo の planes[i].data / stride / width / height を読んで
  // 任意の宛先 (CPU バッファ / GL テクスチャ) にコピーする。 callback から
  // return した時点で planes[i].data は無効になるので、 必要なら同期的に copy
  // しておくこと。
  //
  // packed (ARGB / ABGR / RGBA / BGRA): planeCount=1、 planes[0] が全体パック。
  //                                     bytesPerPixel=4、 stride は通常 w*4。
  // I420  (planar):  planeCount=3、 planes[0]=Y(w×h)、 [1]=U(w/2×h/2)、 [2]=V(w/2×h/2)。
  //                                 bytesPerPixel=1。
  // NV12  (semi-planar): planeCount=2、 [0]=Y(w×h)、 [1]=UV interleaved(w/2×h/2、 2 byte/pel)。
  // NV21  (semi-planar): planeCount=2、 [0]=Y(w×h)、 [1]=VU interleaved。
  //
  // **SetOnVideoDecoded と SetOnVideoDecodedPlanes は排他**。 最後に呼んだ方の callback
  // のみが有効になる (内部の slot を上書きする)。 同時利用は不可。
  struct VideoFrameInfo
  {
    int width;
    int height;
    ColorFormat colorFormat;
    int planeCount;
    struct PlaneRef
    {
      const uint8_t *data;
      int width;
      int height;
      int stride;
    } planes[VIDEO_PLANE_COUNT];
  };
  typedef std::function<void(const VideoFrameInfo &frame)> OnVideoDecodedPlanes;
  virtual void SetOnVideoDecodedPlanes(OnVideoDecodedPlanes callback) = 0;

  // 音声出力は InitParam::audioSink (IAudioSink*) を経由する。
  // SetOnAudioDecoded API は撤去された。

  static IMoviePlayer *CreateMoviePlayer(const char *filename, InitParam &param);

  static IMoviePlayer *CreateMoviePlayer(IMovieReadStream *stream, InitParam &param);
};

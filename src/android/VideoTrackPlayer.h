#pragma once

#include "TrackPlayer.h"
#include "CommonUtils.h"

// -----------------------------------------------------------------------------
// デコード済みフレームデータ
// -----------------------------------------------------------------------------
struct DecodedFrame
{
  ssize_t bufferId;
  int32_t width;
  int32_t height;
  int32_t stride;
  PixelFormat colorFormat;
  ColorRange colorRange;
  ColorSpace colorSpace;
  size_t sizeBytes;
  int64_t presentationTimeUs;
  uint8_t *data;
  std::mutex dataMutex;

  DecodedFrame()
  : data(nullptr)
  , sizeBytes(0)
  {}
  ~DecodedFrame() {}

  void Init(int32_t w, int32_t h, int32_t s, PixelFormat cf, ColorRange cr,
            ColorSpace cs);
  void SetData(AMediaCodec *codec, ssize_t id, uint8_t *d, size_t size, int64_t pts);
  void Release(AMediaCodec *codec);

  // 正当性チェック。内部でロックはしないので利用者側でdataMutexロックをかけること
  // (validチェック＋その後の操作 をアトミックにするための仕様)
  bool IsValid();
};

// -----------------------------------------------------------------------------
// ビデオトラックプレイヤ
// -----------------------------------------------------------------------------
class VideoTrackPlayer : public TrackPlayer
{
public:
  VideoTrackPlayer(AMediaExtractor *ex, int32_t trackIndex, MediaClock *timer);
  virtual ~VideoTrackPlayer();

  void Init();
  void Done();

  virtual void HandleOutputData(ssize_t bufIdx, AMediaCodecBufferInfo &bufInfo,
                                int flags) override;

  DecodedFrame *GetDecodedFrame();

  int32_t Width() const;
  int32_t Height() const;

private:
  virtual void HandleMessage(int32_t what, int64_t arg, void *data) override;

private:
  // video 情報
  int32_t mWidth, mHeight;

  // output 情報
  int32_t mOutputWidth, mOutputHeight;
  int32_t mOutputStride;
  int32_t mOutputColorFormat;
  int32_t mOutputColorRange;
  int32_t mOutputColorSpace;

  // デコード済みフレーム情報
  DecodedFrame mDecodedFrame;
};

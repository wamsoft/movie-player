#pragma once

#include "TrackPlayer.h"
#include "CommonUtils.h"
#include "IMoviePlayer.h"

#include <vector>

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

  int32_t Width() const;
  int32_t Height() const;

  // 旧 API (DestUpdater 経路): packed RGBA を host バッファに直接書き込ませる。
  // YUV → RGB 変換は updater 実行中に行う (余計な memcpy 無し)。
  void SetOnVideoDecoded(IMoviePlayer::OnVideoDecoded func, PixelFormat format) {
    mOnVideoDecoded = func;
    mOnVideoDecodedPlanes = nullptr;  // 排他
    mOnVideoFormat = format;
  }
  // 新 API (VideoFrameInfo 経路): YUV plane / packed RGBA を生で渡す。
  void SetOnVideoDecodedPlanes(IMoviePlayer::OnVideoDecodedPlanes func, PixelFormat format) {
    mOnVideoDecodedPlanes = func;
    mOnVideoDecoded = nullptr;  // 排他
    mOnVideoFormat = format;
  }

  // 音声トラックが clock anchor を駆動するかどうか。
  // MoviePlayerCore::Open() で AudioTrackPlayer の有無 (および sink 確立) を
  // 確認したあと呼ぶ。デフォルトは false (= video-master fallback)。
  void SetHasAudio(bool hasAudio) { mHasAudio = hasAudio; }

private:
  virtual void HandleMessage(int32_t what, int64_t arg, void *data) override;

  // 1 フレーム分のメディア時間長。FRAME_RATE が取れなければ 33333us (~30fps)。
  int64_t FrameDurationUs() const { return mFrameDurationUs; }

  // 出力フレームの提示判定の中身 (codec output buffer の release / callback)。
  void PresentFrame(ssize_t bufIdx, AMediaCodecBufferInfo &bufInfo);

private:
  // video 情報
  int32_t mWidth, mHeight;

  // output 情報
  int32_t mOutputWidth, mOutputHeight;
  int32_t mOutputStride;
  int32_t mOutputColorFormat;
  int32_t mOutputColorRange;
  int32_t mOutputColorSpace;

  // 同期用
  bool    mHasAudio;          // audio-master の場合 true
  int64_t mFrameDurationUs;   // 1 フレームの想定メディア時間長

  // 動画コールバック (旧 / 新の排他、 nullptr でない方が使われる)
  IMoviePlayer::OnVideoDecoded       mOnVideoDecoded;
  IMoviePlayer::OnVideoDecodedPlanes mOnVideoDecodedPlanes;
  PixelFormat mOnVideoFormat;

  // 新 API + RGBA 出力モード時に YUV→RGBA 変換した結果を一時保持するバッファ
  // (新 API は plane 参照を渡すので callback 実行中に有効なメモリが必要)。
  // YUV 出力モード (I420/NV12/NV21) では codec output buffer をそのまま参照する。
  std::vector<uint8_t> mPackedBuffer;
};

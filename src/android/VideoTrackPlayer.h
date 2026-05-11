#pragma once

#include "TrackPlayer.h"
#include "CommonUtils.h"
#include "IMoviePlayer.h"

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

  void SetOnVideoDecoded(IMoviePlayer::OnVideoDecoded func, PixelFormat format) {
    mOnVideoDecoded = func;
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

  // オーディオコールバック
  IMoviePlayer::OnVideoDecoded mOnVideoDecoded;
  PixelFormat mOnVideoFormat;
};

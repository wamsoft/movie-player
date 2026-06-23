#define MYLOG_TAG "VideoTrackPlayer"
#include "BasicLog.h"

#include "CommonUtils.h"
#include "Constants.h"
#include "VideoTrackPlayer.h"
#include "PixelConvert.h"

// -----------------------------------------------------------------------------
// カラー関連定数変換処理
// -----------------------------------------------------------------------------
// MediaCodecの内部カラーフォーマットからCommonのPixelFormatへ変換
static PixelFormat
conv_color_format(int32_t nativeFormat)
{
  // これ自体は単純な変換だが、Android環境では特定のデバイスから
  // 想定外データが来る可能性はあるので、一応ASSERTで捕まえるようにしてある

  // MEMO
  // Packed: YUV,YUV,YUV... インタリーブ配置される
  // Planer: YY..., UU..., VV... プレーン配置される
  // Semi-: YY..., UVUV... YとUVはプレーン配置で、UV側内部はインタリーブ配置される
  // Packed(Semi)Planer: ??? 過去互換的、歴史的な名称っぽい？

  PixelFormat pixelFormat = PIXEL_FORMAT_UNKNOWN;

  switch (nativeFormat) {
  case COLOR_FormatYUV420Flexible:
  case COLOR_FormatYUV420Planar:
    pixelFormat = PIXEL_FORMAT_I420;
    break;
  case COLOR_FormatYUV420SemiPlanar:
    pixelFormat = PIXEL_FORMAT_NV12;
    break;
  case COLOR_FormatYUV422Planar:
    pixelFormat = PIXEL_FORMAT_I422;
    break;
  case COLOR_FormatYUV444Flexible:
  case COLOR_FormatYUV444Interleaved:
    pixelFormat = PIXEL_FORMAT_I444;
    break;
  default:
    ASSERT(false, "*** Unsupported color format!! format=%d ***", nativeFormat);
    break;
  }

  return pixelFormat;
}

// MediaCodecの内部カラースペースからCommonのColorSpaceへ変換
static ColorSpace
conv_color_space(int32_t nativeColorSpace)
{
  ColorSpace cs = COLOR_SPACE_BT_601;

  switch (nativeColorSpace) {
  case COLOR_STANDARD_BT709:
    cs = COLOR_SPACE_BT_709;
    break;
  case COLOR_STANDARD_BT601_PAL:
  case COLOR_STANDARD_BT601_NTSC:
    cs = COLOR_SPACE_BT_601;
    break;
  case COLOR_STANDARD_BT2020:
    cs = COLOR_SPACE_BT_2020;
    break;
  }

  return cs;
}

// MediaCodecの内部カラーレンジからCommonのColorRangeへ変換
static ColorRange
conv_color_range(int32_t nativeColorRange)
{
  ColorRange cr = COLOR_RANGE_LIMITED;

  switch (nativeColorRange) {
  case MEDIA_COLOR_RANGE_FULL:
    cr = COLOR_RANGE_FULL;
    break;
  default:
    cr = COLOR_RANGE_LIMITED;
    break;
  }

  return cr;
}

// -----------------------------------------------------------------------------
// ビデオトラックプレイヤ
// -----------------------------------------------------------------------------
VideoTrackPlayer::VideoTrackPlayer(AMediaExtractor *ex, int32_t trackIndex,
                                   MediaClock *timer)
: TrackPlayer(ex, trackIndex, timer)
{
  Init();

  AMediaExtractor_selectTrack(mExtractor, mTrackIndex);

  LOGV("video track: %d", mTrackIndex);

  // トラックフォーマット
  AMediaFormat *format = AMediaExtractor_getTrackFormat(mExtractor, mTrackIndex);
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &mWidth);
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &mHeight);
  AMediaFormat_getInt64(format, AMEDIAFORMAT_KEY_DURATION, &mDuration);

  // フレームレート (float / int どちらの key で来るか不定なので両方試す)。
  // 取れない場合は 30fps 相当をデフォルトとする。
  {
    float fpsF       = 0.0f;
    int32_t fpsI     = 0;
    if (AMediaFormat_getFloat(format, AMEDIAFORMAT_KEY_FRAME_RATE, &fpsF) && fpsF > 0.0f) {
      mFrameDurationUs = (int64_t)(1'000'000.0f / fpsF);
    } else if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, &fpsI) && fpsI > 0) {
      mFrameDurationUs = 1'000'000 / fpsI;
    } else {
      mFrameDurationUs = 33'333; // ~30fps
    }
  }

  LOGV("video duration = %lld, frame duration = %lld us", mDuration, mFrameDurationUs);

  // 現在のメディアタイマーにセットされているDurationよりも長い場合は
  // それをムービー全体のDurationとして反映する
  if (mDuration > mClock->GetDuration()) {
    mClock->SetDuration(mDuration);
  }

  // コーデックセットアップ
  const char *mime;
  AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
  mCodec = AMediaCodec_createDecoderByType(mime);
  AMediaCodec_configure(mCodec, format, nullptr, nullptr, 0);
  AMediaCodec_start(mCodec);

  // 出力フォーマット
  AMediaFormat *outFormat = AMediaCodec_getOutputFormat(mCodec);
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_WIDTH, &mOutputWidth);
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_HEIGHT, &mOutputHeight);
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_STRIDE, &mOutputStride);
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_COLOR_FORMAT, &mOutputColorFormat);
#if __ANDROID_API__>= __ANDROID_API_P__ //28
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_COLOR_RANGE, &mOutputColorRange);
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_COLOR_STANDARD, &mOutputColorSpace);
#endif

  LOGV("video w=%d, h=%d, stride=%d, colorFormat=%d, colorRange=%d, colorSpace=%d",
       mOutputWidth, mOutputHeight, mOutputStride, mOutputColorFormat, mOutputColorRange,
       mOutputColorSpace);
}

VideoTrackPlayer::~VideoTrackPlayer()
{
  Done();
}

void
VideoTrackPlayer::Init()
{
  mWidth  = -1;
  mHeight = -1;

  mOutputWidth       = -1;
  mOutputHeight      = -1;
  mOutputStride      = -1;
  mOutputColorFormat = -1;
  mOutputColorRange  = -1;
  mOutputColorSpace  = -1;

  mHasAudio        = false;
  mFrameDurationUs = 33'333; // ctor で FRAME_RATE 取得時に上書きされる

  mOnVideoDecoded = nullptr;
  mOnVideoFormat = PIXEL_FORMAT_UNKNOWN;
}

void
VideoTrackPlayer::Done()
{}

int32_t
VideoTrackPlayer::Width() const
{
  if (IsValid()) {
    return mOutputWidth;
  } else {
    return -1;
  }
}

int32_t
VideoTrackPlayer::Height() const
{
  if (IsValid()) {
    return mOutputHeight;
  } else {
    return -1;
  }
}

void
VideoTrackPlayer::HandleMessage(int32_t what, int64_t arg, void *obj)
{
  // LOGV("handle msg %d", what);

  switch (what) {
  case MSG_START:
    SetState(STATE_PLAY);
    Decode();
    mEventFlag.Set(EVENT_FLAG_PLAY_READY);
    break;

  case MSG_PAUSE:
    if (GetState() == STATE_PLAY) {
      SetState(STATE_PAUSE);
      Post(MSG_NOP, 0, nullptr, true); // 発行済メッセージを全フラッシュ
    }
    break;

  case MSG_RESUME:
    if (GetState() == STATE_PAUSE) {
      mClock->Reset();
      SetState(STATE_PLAY);
      Decode();
    }
    break;

  case MSG_SEEK:
    AMediaExtractor_seekTo(mExtractor, arg, AMEDIAEXTRACTOR_SEEK_NEXT_SYNC);
    AMediaCodec_flush(mCodec);
    // TODO starttimeをV/A共通にしてるので、同期処理していない現状ではおかしくなるかも？
    mClock->Reset();
    mSawInputEOS  = false;
    mSawOutputEOS = false;
    if (GetState() != STATE_PLAY) {
      // 再生中ではない場合は、シーク動作のために一度だけデコード処理をする
      Decode(DECODER_FLAG_ONESHOT);
    }
    break;

  case MSG_STOP:
    SetState(STATE_STOP);
    AMediaCodec_stop(mCodec);
    // AMediaCodec_delete(mCodec);
    Post(MSG_NOP, 0, nullptr, true); // 発行済メッセージを全フラッシュ
    mEventFlag.Set(EVENT_FLAG_STOPPED);
    break;

  default:
    TrackPlayer::HandleMessage(what, arg, obj);
    break;
  }

  std::this_thread::yield();
}

void
VideoTrackPlayer::HandleOutputData(ssize_t bufIdx, AMediaCodecBufferInfo &bufInfo,
                                   int32_t flags)
{
  if (bufInfo.size <= 0) {
    // EOS マーカー等。codec slot だけ release。
    AMediaCodec_releaseOutputBuffer(mCodec, bufIdx, false);
    return;
  }

  int64_t mediaTimeUs = bufInfo.presentationTimeUs;
  int64_t frameDurUs  = FrameDurationUs();

  // -------- 同期判定 --------
  // mClock が起動していなければ初回フレームとして無条件提示し、
  // start media time をセットする (audio-master の場合は audio 側が
  // anchor を貼るのを待つが、最初の絵だけは出してしまう)。
  // 音声無し時は自身が anchor source となる。
  enum { ACT_PRESENT, ACT_SKIP } action = ACT_PRESENT;

  if (!mClock->IsStarted()) {
    mClock->SetStartMediaTime(mediaTimeUs);
    if (!mHasAudio) {
      int64_t nowUs    = get_time_us();
      mClock->UpdateAnchorTime(mediaTimeUs, nowUs, mediaTimeUs + frameDurUs);
    }
    // ACT_PRESENT のまま (初回フレームは無条件出す)
  } else {
    // 既に anchor あり。提示予定時刻 (real time) と現在 real time の差で判定。
    int64_t targetRealUs = mClock->GetRealTimeFor(mediaTimeUs);
    int64_t nowUs        = get_time_us();
    int64_t diffUs       = nowUs - targetRealUs; // + 遅れ, - 早い

    if (diffUs >= frameDurUs) {
      // 1 フレーム以上遅れ → スキップ
      action = ACT_SKIP;
    } else if (diffUs < 0) {
      // 早すぎ → 提示時刻まで sleep。
      // MSG_STOP/PAUSE/SEEK の応答遅延を抑えるため、上限を 4 フレーム相当に
      // キャップする (異常な PTS 値が来た時の保険)。
      int64_t sleepUs = -diffUs;
      int64_t maxWait = frameDurUs * 4;
      if (sleepUs > maxWait) sleepUs = maxWait;
      std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
    }
  }

  if (action == ACT_SKIP) {
    AMediaCodec_releaseOutputBuffer(mCodec, bufIdx, false);
    // 音声無し時はスキップしても PTS だけは進めておく (anchor は更新しない)
    if (!mHasAudio) {
      mClock->SetPresentationTime(mediaTimeUs);
    }
    return;
  }

  // -------- 提示 --------
  mClock->SetPresentationTime(mediaTimeUs);
  if (!mHasAudio) {
    // video-master: 自身が anchor を担当
    int64_t nowUs2 = get_time_us();
    mClock->UpdateAnchorTime(mediaTimeUs, nowUs2, mediaTimeUs + frameDurUs);
  }

  PresentFrame(bufIdx, bufInfo);
}

void
VideoTrackPlayer::PresentFrame(ssize_t bufIdx, AMediaCodecBufferInfo &bufInfo)
{
  if (mOnVideoDecoded == nullptr && mOnVideoDecodedPlanes == nullptr) {
    AMediaCodec_releaseOutputBuffer(mCodec, bufIdx, false);
    return;
  }

  size_t bufSize;
  uint8_t *buf = AMediaCodec_getOutputBuffer(mCodec, bufIdx, &bufSize);
  // YUVのプレーンごとのストライドなどを取得する方法が見当たらないので
  // ソースプレーンがパディングされずに連続して配置されるものとして扱っている
  // 一部デバイスや特殊サイズムービーだとうまくいかない可能性あり
  const uint8_t *yBuf = buf + bufInfo.offset;
  const uint8_t *uBuf = yBuf + mOutputWidth * mOutputHeight;
  const uint8_t *vBuf = uBuf + (mOutputWidth * mOutputHeight / 4);
  int32_t yStride     = mOutputWidth;
  int32_t uvStride    = mOutputWidth / 2;
  PixelFormat colorFormat = conv_color_format(mOutputColorFormat);
  if (is_nv_pixel_format(colorFormat)) {
    uvStride = mOutputWidth; // NVはUVがパックド
  }
  int W = mOutputWidth, H = mOutputHeight;
  int W2 = (W + 1) / 2, H2 = (H + 1) / 2;

  if (mOnVideoDecoded) {
    // 旧 API: updater 経由で host バッファに直接 YUV→RGB 変換
    mOnVideoDecoded(W, H, [&](char *dest, int pitch) {
      convert_yuv_to_rgb32((uint8_t*)dest, pitch, mOnVideoFormat, yBuf, uBuf, vBuf, 0,
                          yStride, uvStride, 0, W, H, colorFormat,
                          (ColorSpace)mOutputColorSpace, (ColorRange)mOutputColorRange);
    });
  } else {
    // 新 API: VideoFrameInfo 経由で plane を直接渡す。
    // RGBA 要求時のみ mPackedBuffer に YUV→RGBA 変換、 YUV 要求時は codec output を直参照。
    IMoviePlayer::VideoFrameInfo info{};
    info.width  = W;
    info.height = H;
    info.colorFormat = to_imovie_color_format(mOnVideoFormat);
    if (!is_yuv_pixel_format(mOnVideoFormat)) {
      if (mPackedBuffer.size() != (size_t)W * H * 4) mPackedBuffer.assign((size_t)W * H * 4, 0);
      convert_yuv_to_rgb32(mPackedBuffer.data(), W * 4, mOnVideoFormat, yBuf, uBuf, vBuf, 0,
                          yStride, uvStride, 0, W, H, colorFormat,
                          (ColorSpace)mOutputColorSpace, (ColorRange)mOutputColorRange);
      info.planeCount = 1;
      info.planes[IMoviePlayer::VIDEO_PLANE_PACKED] = { mPackedBuffer.data(), W, H, W * 4 };
    } else if (mOnVideoFormat == PIXEL_FORMAT_I420) {
      info.planeCount = 3;
      info.planes[IMoviePlayer::VIDEO_PLANE_Y] = { yBuf, W,  H,  yStride };
      info.planes[IMoviePlayer::VIDEO_PLANE_U] = { uBuf, W2, H2, uvStride };
      info.planes[IMoviePlayer::VIDEO_PLANE_V] = { vBuf, W2, H2, uvStride };
    } else {
      // NV12 / NV21: Y + UV interleaved
      info.planeCount = 2;
      info.planes[IMoviePlayer::VIDEO_PLANE_Y] = { yBuf, W,  H,  yStride };
      info.planes[1]                           = { uBuf, W2, H2, uvStride };
    }
    mOnVideoDecodedPlanes(info);
  }
  AMediaCodec_releaseOutputBuffer(mCodec, bufIdx, false);
}

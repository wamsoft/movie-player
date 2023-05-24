#pragma once

#include "CommonUtils.h"

#include <cstdint>
#include <chrono>
#include <mutex>

class MediaClock
{
public:
  MediaClock()
  : mDurationUs(-1)
  , mStartMediaTimeUs(-1)
  , mStartSystemTimeUs(-1)
  {}
  ~MediaClock() {}

  void SetDuration(int64_t mediaDurationUs)
  {
    std::lock_guard<std::mutex> lock(mLock);

    mDurationUs = mediaDurationUs;
  }

  int64_t GetDuration() const
  {
    std::lock_guard<std::mutex> lock(mLock);

    return mDurationUs;
  }

  void SetStartTime(int64_t startMediaTimeUs = 0, int64_t nowUs = -1)
  {
    std::lock_guard<std::mutex> lock(mLock);

    if (nowUs < 0) {
      nowUs = get_time_us();
    }
    mStartSystemTimeUs = nowUs - startMediaTimeUs;
    mStartMediaTimeUs  = startMediaTimeUs;
  }

  void SetStartMediaTime(int64_t startMediaTimeUs)
  {
    std::lock_guard<std::mutex> lock(mLock);

    mStartMediaTimeUs  = startMediaTimeUs;
  }

  void SetStartSystemTime(int64_t nowUs = -1)
  {
    std::lock_guard<std::mutex> lock(mLock);

    if (nowUs < 0) {
      nowUs = get_time_us();
    }
    mStartSystemTimeUs = nowUs;
  }

  int64_t GetStartSystemTime() const
  {
    std::lock_guard<std::mutex> lock(mLock);

    return mStartSystemTimeUs;
  }

  int64_t GetStartMediaTime() const
  {
    std::lock_guard<std::mutex> lock(mLock);

    return mStartMediaTimeUs;
  }

  void UpdateStartTime(int64_t startMediaTimeUs) { SetStartTime(startMediaTimeUs); }

  void ClearStartSystemTime()
  {
    std::lock_guard<std::mutex> lock(mLock);

    mStartSystemTimeUs = -1;
  }

  void ClearStartMediaTime()
  {
    std::lock_guard<std::mutex> lock(mLock);

    mStartMediaTimeUs  = -1;
  }

  // 自分のTimeと現在のメディアタイムとの差を計算する
  // Audioがある環境で、Videoフレームの更新タイミングをAudio PTSから計算するために使用
  int64_t CalcDiffFromMediaTime(int64_t myTimeUs)
  {
    std::lock_guard<std::mutex> lock(mLock);

    if (!is_started()) {
      assert(false);
      INLINE_LOGE("BUG?: CalcDiffFromMediaTime(): start time is not set.\n");
      return -1;
    }

    return mCurrentMediaTimeUs - myTimeUs;
  }

  // 自分のTimeと現在のシステム時間との差を計算する
  // Audioが無い環境で、Videoフレームの更新タイミングを計算するために使用
  int64_t CalcDiffFromSystemTime(int64_t myTimeUs)
  {
    std::lock_guard<std::mutex> lock(mLock);

    if (!is_started()) {
      assert(false);
      INLINE_LOGE("BUG?: CalcDiffFromMediaTime(): start time is not set.\n");
      return -1;
    }

    int64_t nowSysTimeUs = get_time_us();
    return nowSysTimeUs - (mStartSystemTimeUs + myTimeUs);
  }

  bool IsStarted() const
  {
    std::lock_guard<std::mutex> lock(mLock);

    return is_started();
  }

  int64_t ConvMediaToSystem(int64_t mediaTimeUs)
  {
    std::lock_guard<std::mutex> lock(mLock);

    if (!is_started()) {
      INLINE_LOGV("BUG?: ConvMediaToSystem(): start time is not set.\n");
      return -1;
    }

    return mStartSystemTimeUs + mediaTimeUs;
  }

  int64_t ConvSystemToMedia(int64_t systemTimeUs)
  {
    std::lock_guard<std::mutex> lock(mLock);

    if (!is_started()) {
      INLINE_LOGV("BUG?: ConvSystemToMedia(): start time is not set.\n");
      return -1;
    }

    // mediaのstart<->duration区間を越えた時間はクリップしておく
    int64_t mediaUs = (systemTimeUs - mStartSystemTimeUs) + mStartMediaTimeUs;
    if (mediaUs > mDurationUs) {
      mediaUs = mDurationUs;
    } else if (mediaUs < mStartMediaTimeUs) {
      mediaUs = mStartMediaTimeUs;
    }

    return mediaUs;
  }

  void SetCurrentMediaTime(int64_t currentMediaTimeUS)
  {
    std::lock_guard<std::mutex> lock(mLock);

    mCurrentMediaTimeUs = currentMediaTimeUS;
  }

  int64_t GetCurrentMediaTime() const
  {
    std::lock_guard<std::mutex> lock(mLock);

    return mCurrentMediaTimeUs;
  }

  int64_t GetCurrentSystemTime() const { return get_time_us(); }

#if 1 // TODO OBSOLETE
  void ClearStartTime()
  {
    std::lock_guard<std::mutex> lock(mLock);

    mStartMediaTimeUs  = -1;
    mStartSystemTimeUs = -1;
  }

  int64_t CalcDelay(int64_t mediaTimeUs, int64_t nowUs = -1)
  {
    std::lock_guard<std::mutex> lock(mLock);

    if (!is_started()) {
      assert(false);
      INLINE_LOGE("BUG?: CalcDelay(): start time is not set.\n");
      return -1;
    }

    if (nowUs < 0) {
      nowUs = get_time_us();
    }

    return (mStartSystemTimeUs + mediaTimeUs) - nowUs;
  }
#endif

private:
  bool is_started() const { return (mStartMediaTimeUs >= 0 && mStartSystemTimeUs >= 0); }

private:
  mutable std::mutex mLock;

  int64_t mDurationUs;
  int64_t mStartMediaTimeUs;
  int64_t mStartSystemTimeUs;
  int64_t mCurrentMediaTimeUs;
};
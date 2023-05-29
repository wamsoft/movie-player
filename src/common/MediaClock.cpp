#define MYLOG_TAG "MediaClock"
#include "BasicLog.h"
#include "CommonUtils.h"
#include "MediaClock.h"

// -----------------------------------------------------------------------------
// MediaClock
// -----------------------------------------------------------------------------
MediaClock::MediaClock()
: mDurationUs(-1)
, mStartMediaTimeUs(-1)
, mStartSystemTimeUs(-1)
, mAnchorMediaTimeUs(-1)
, mAnchorSystemTimeUs(-1)
, mResetCount(0)
{}

MediaClock::~MediaClock() {}

void
MediaClock::Reset()
{
  std::lock_guard<std::mutex> lock(mLock);

  mDurationUs         = -1;
  mStartMediaTimeUs   = -1;
  mStartSystemTimeUs  = -1;
  mAnchorMediaTimeUs  = -1;
  mAnchorSystemTimeUs = -1;

  mResetCount++;
}

void
MediaClock::SetDuration(int64_t mediaDurationUs)
{
  std::lock_guard<std::mutex> lock(mLock);

  mDurationUs = mediaDurationUs;
}

int64_t
MediaClock::GetDuration() const
{
  std::lock_guard<std::mutex> lock(mLock);

  return mDurationUs;
}

int64_t
MediaClock::GetStartSystemTime() const
{
  std::lock_guard<std::mutex> lock(mLock);

  return mStartSystemTimeUs;
}

int64_t
MediaClock::GetStartMediaTime() const
{
  std::lock_guard<std::mutex> lock(mLock);

  return mStartMediaTimeUs;
}

void
MediaClock::SetStartTime(int64_t startMediaTimeUs, int64_t startSystemTimeUs)
{
  std::lock_guard<std::mutex> lock(mLock);

  if (startSystemTimeUs < 0) {
    startSystemTimeUs = get_time_us();
  }
  mStartSystemTimeUs = startSystemTimeUs - startMediaTimeUs;
  mStartMediaTimeUs  = startMediaTimeUs;
}

void
MediaClock::SetAnchorTime(int64_t anchorMediaTimeUs, int64_t anchorSystemTimeUs)
{
  if (anchorMediaTimeUs < 0 || anchorSystemTimeUs < 0) {
    ASSERT(false, "BUG: invalid anchor time set.\n");
    return;
  }

  std::lock_guard<std::mutex> lock(mLock);

  if (anchorSystemTimeUs < 0) {
    anchorSystemTimeUs = get_time_us();
  }
  mAnchorSystemTimeUs = anchorSystemTimeUs;
  mAnchorMediaTimeUs  = anchorMediaTimeUs;
}

int64_t
MediaClock::CalcDiffFromMediaTime(int64_t myTimeUs)
{
  std::lock_guard<std::mutex> lock(mLock);

  if (!is_started()) {
    assert(false);
    INLINE_LOGE("BUG?: CalcDiffFromMediaTime(): start time is not set.\n");
    return -1;
  }

  return mAnchorMediaTimeUs - myTimeUs;
}

int64_t
MediaClock::CalcDiffFromSystemTime(int64_t myTimeUs)
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
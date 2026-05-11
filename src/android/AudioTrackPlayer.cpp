#define MYLOG_TAG "AudioTrackPlayer"
#include "BasicLog.h"

#include "Constants.h"
#include "CommonUtils.h"
#include "AudioTrackPlayer.h"
#include "IAudioSink.h"

AudioTrackPlayer::AudioTrackPlayer(AMediaExtractor *ex, int32_t trackIndex,
                                   MediaClock *timer, IAudioSink *audioSink)
: TrackPlayer(ex, trackIndex, timer)
, mAudioSink(audioSink)
{
  Init();

  AMediaExtractor_selectTrack(mExtractor, mTrackIndex);

  LOGV("audio track: %d", mTrackIndex);

  // トラックフォーマット
  AMediaFormat *format = AMediaExtractor_getTrackFormat(mExtractor, mTrackIndex);
  AMediaFormat_getInt64(format, AMEDIAFORMAT_KEY_DURATION, &mDuration);

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
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_SAMPLE_RATE, &mSampleRate);
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &mChannels);
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, &mMaxInputSize);

#ifdef AMEDIAFORMAT_KEY_BITS_PER_SAMPLE
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_BITS_PER_SAMPLE, &mBitsPerSample);
#else
  AMediaFormat_getInt32(outFormat, "bits-per-sample", &mBitsPerSample);
#endif
  if (mBitsPerSample <= 0) {
    mBitsPerSample = 16; // デフォルト16bitとしておく
  }

#ifdef AMEDIAFORMAT_KEY_PCM_ENCODING
  AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_PCM_ENCODING, &mEncoding);
#else
  AMediaFormat_getInt32(outFormat, "pcm-encoding", &mEncoding);
#endif
  if (mEncoding <= 0) {
    mEncoding = kAudioEncodingPcm16bit;
  }

  LOGV("audio duration=%lld, sampleRate=%d, channels=%d, bps=%d\n", mDuration,
       mSampleRate, mChannels, mBitsPerSample);

  // sink へ format 通知。失敗したら audio 無し再生に切替。
  if (mAudioSink) {
    IAudioSink::Encoding encoding = IAudioSink::PCM_S16;
    switch (mEncoding) {
    case kAudioEncodingPcm8bit:  encoding = IAudioSink::PCM_U8;  break;
    case kAudioEncodingPcm16bit: encoding = IAudioSink::PCM_S16; break;
    case kAudioEncodingPcm32bit: encoding = IAudioSink::PCM_S32; break;
    case kAudioEncodingPcmFloat: encoding = IAudioSink::PCM_F32; break;
    default: break;
    }
    if (!mAudioSink->Setup(mChannels, mSampleRate, mBitsPerSample, encoding)) {
      LOGE("audio sink setup failed; disabling audio output\n");
      mAudioSink = nullptr;
    }
  }
}

AudioTrackPlayer::~AudioTrackPlayer()
{
  Done();
}

void
AudioTrackPlayer::Init()
{
  mDuration = -1;

  mChannels      = -1;
  mSampleRate    = -1;
  mBitsPerSample = -1;
  mMaxInputSize  = -1;

  mStartPtsValid     = false;
  mStartPtsUs        = -1;
  mSamplesPlayedBase = 0;
}

void
AudioTrackPlayer::Done()
{
  // sink がまだ pending している decoder buffer は、AMediaCodec_stop で
  // 暗黙に release されるのでここで明示的にループを drain する必要はない。
}

void
AudioTrackPlayer::DrainAudioSinkConsumed()
{
  if (!mAudioSink || !mCodec) return;
  void *param = nullptr;
  while (mAudioSink->TryPopConsumed(&param)) {
    ssize_t bufIdx = (ssize_t)(intptr_t)param;
    AMediaCodec_releaseOutputBuffer(mCodec, bufIdx, false);
  }
}

void
AudioTrackPlayer::InvalidateStartPts()
{
  mStartPtsValid     = false;
  mStartPtsUs        = -1;
  mSamplesPlayedBase = 0;
}

void
AudioTrackPlayer::UpdateClockFromSink()
{
  if (!mAudioSink || !mStartPtsValid || mSampleRate <= 0 || mClock == nullptr) {
    return;
  }

  int64_t samplesPlayed = mAudioSink->GetSamplesPlayed();
  int64_t delta         = samplesPlayed - mSamplesPlayedBase;
  if (delta < 0) {
    // sink 内部でカウンタが巻き戻された場合の防御
    delta              = 0;
    mSamplesPlayedBase = samplesPlayed;
  }
  int64_t playedUs    = calc_audio_duration_us(delta, mSampleRate);
  int64_t mediaTimeUs = mStartPtsUs + playedUs;
  if (mediaTimeUs < 0) return;

  if (!mClock->IsStarted()) {
    mClock->SetStartMediaTime(mStartPtsUs);
  }
  mClock->SetPresentationTime(mediaTimeUs);

  int64_t nowUs          = get_time_us();
  // 安全マージンとして +100ms 先までを max とする (windows 実装に合わせる)
  int64_t maxMediaTimeUs = mediaTimeUs + 100000;
  mClock->UpdateAnchorTime(mediaTimeUs, nowUs, maxMediaTimeUs);
}

int32_t
AudioTrackPlayer::SampleRate() const
{
  if (IsValid()) {
    return mSampleRate;
  } else {
    return -1;
  }
}

int32_t
AudioTrackPlayer::Channels() const
{
  if (IsValid()) {
    return mChannels;
  } else {
    return -1;
  }
}

int32_t
AudioTrackPlayer::BitsPerSample() const
{
  if (IsValid()) {
    return mBitsPerSample;
  } else {
    return -1;
  }
}

int32_t
AudioTrackPlayer::Encoding() const
{
  if (IsValid()) {
    return mEncoding;
  } else {
    return -1;
  }
}

int32_t
AudioTrackPlayer::MaxInputSize() const
{
  if (IsValid()) {
    return mMaxInputSize;
  } else {
    return -1;
  }
}

void
AudioTrackPlayer::HandleMessage(int32_t what, int64_t arg, void *obj)
{
  // sink が積んだ完了通知を回収して codec output buffer を release する。
  // (decoder thread で行うことで audio callback の負荷を抑える)
  DrainAudioSinkConsumed();
  // 再生継続中に sink->GetSamplesPlayed を anchor に反映 (HandleOutputData
  // 経由だけだと codec が一時的に何も出さない時間に clock が更新されない)
  UpdateClockFromSink();

  switch (what) {
  case MSG_START:
    SetState(STATE_PLAY);
    if (mAudioSink) mAudioSink->Start();
    Decode();
    mEventFlag.Set(EVENT_FLAG_PLAY_READY);
    break;

  case MSG_PAUSE:
    if (GetState() == STATE_PLAY) {
      SetState(STATE_PAUSE);
      if (mAudioSink) mAudioSink->Stop();
      Post(MSG_NOP, 0, nullptr, true); // 発行済メッセージを全フラッシュ
    }
    break;

  case MSG_RESUME:
    if (GetState() == STATE_PAUSE) {
      // pause 中も sink の累積 sample counter は進む可能性があるため
      // resume 直後に baseline を取り直す
      InvalidateStartPts();
      mClock->Reset();
      SetState(STATE_PLAY);
      if (mAudioSink) mAudioSink->Start();
      Decode();
    }
    break;

  case MSG_SEEK:
    AMediaExtractor_seekTo(mExtractor, arg, AMEDIAEXTRACTOR_SEEK_NEXT_SYNC);
    AMediaCodec_flush(mCodec);
    if (mAudioSink) mAudioSink->Flush();
    InvalidateStartPts();
    mClock->Reset();
    mSawInputEOS  = false;
    mSawOutputEOS = false;
    break;

  case MSG_STOP:
    SetState(STATE_STOP);
    if (mAudioSink) mAudioSink->Stop();
    AMediaCodec_stop(mCodec);
    // AMediaCodec_delete(mCodec);
    InvalidateStartPts();
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
AudioTrackPlayer::HandleOutputData(ssize_t bufIdx, AMediaCodecBufferInfo &bufInfo,
                                   int32_t flags)
{
  if (bufInfo.size > 0) {
    if (mAudioSink) {
      // sink へ流す。bufIdx を param に乗せ、consumed 通知が返ってきたところで
      // AMediaCodec_releaseOutputBuffer を呼ぶ (DrainAudioSinkConsumed 経由)。
      size_t bufSize = 0;
      uint8_t *buf   = AMediaCodec_getOutputBuffer(mCodec, bufIdx, &bufSize);
      if (buf) {
        bool isLast = (bufInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;

        // clock anchor 用に、起点 PTS と sink の累積サンプル baseline を
        // 一度だけ記録する (Pause/Resume/Seek 後は InvalidateStartPts により
        // 改めてここで再記録される)
        if (!mStartPtsValid) {
          mStartPtsUs        = bufInfo.presentationTimeUs;
          mSamplesPlayedBase = mAudioSink->GetSamplesPlayed();
          mStartPtsValid     = true;
        }

        mAudioSink->Enqueue(buf + bufInfo.offset, bufInfo.size, isLast,
                            (void *)(intptr_t)bufIdx);
      } else {
        // 取得失敗時は codec slot だけ release
        AMediaCodec_releaseOutputBuffer(mCodec, bufIdx, false);
      }
    } else {
      // audio 無し再生: codec output を即時 release
      AMediaCodec_releaseOutputBuffer(mCodec, bufIdx, false);
    }
  } else {
    // size 0 の出力は EOS マーカーなどなので codec slot は release だけしておく
    AMediaCodec_releaseOutputBuffer(mCodec, bufIdx, false);
  }

  // 起点が確定したら audio-master clock を更新
  UpdateClockFromSink();
}

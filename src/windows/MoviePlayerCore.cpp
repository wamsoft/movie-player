#define MYLOG_TAG "MoviePlayerCore"
#include "BasicLog.h"
#include "MoviePlayerCore.h"
#include "AudioEngine.h"

MoviePlayerCore::MoviePlayerCore()
: mState(STATE_UNINIT)
{
  Init();
}

MoviePlayerCore::~MoviePlayerCore()
{
  Done();
}

void
MoviePlayerCore::Init()
{
  mWidth       = -1;
  mHeight      = -1;
  mPixelFormat = PIXEL_FORMAT_UNKNOWN;

  mSawInputEOS  = false;
  mSawOutputEOS = false;

  mIsLoop = false;

  mExtractor    = nullptr;
  mVideoDecoder = nullptr;
  mAudioDecoder = nullptr;

  mClock.Reset();

  mDecodedFrame = mDecodedFrameNext = nullptr;
  mDummyFrame.InitByType(TRACK_TYPE_VIDEO, -1);

  mAudioEngine      = nullptr;
  mAudioQueuedBytes = 0;
  mAudioDataPos     = 0;
  mAudioUnitSize    = 0;
  mAudioTime.Reset();
}

void
MoviePlayerCore::InitDummyFrame()
{
  if (mVideoDecoder) {
    mDummyFrame.InitByType(TRACK_TYPE_VIDEO, -1);
    mDummyFrame.Resize(mWidth * mHeight * 4);
    mDummyFrame.timeStampNs = 0;
    mDummyFrame.frame       = 0;
    memset(mDummyFrame.data, 0xff, mDummyFrame.capacity);
    {
      std::lock_guard<std::mutex> lock(mDecodedFrameMutex);

      mDecodedFrame = &mDummyFrame;
    }
  }
}

void
MoviePlayerCore::Done()
{
  Flush();
  StopThread();

  if (mAudioEngine) {
    mAudioEngine->Stop();
    delete mAudioEngine;
    mAudioEngine = nullptr;
  }

  if (mVideoDecoder) {
    mVideoDecoder->Stop();
    delete mVideoDecoder;
    mVideoDecoder = nullptr;
  }

  if (mAudioDecoder) {
    mAudioDecoder->Stop();
    delete mAudioDecoder;
    mAudioDecoder = nullptr;
  }

  if (mExtractor) {
    delete mExtractor;
    mExtractor = nullptr;
  }
}

void
MoviePlayerCore::Start()
{
  if (mVideoDecoder) {
    mVideoDecoder->Start();
  }

  if (mAudioDecoder) {
    mAudioDecoder->Start();
  }

  StartThread();
}

void
MoviePlayerCore::Play(bool loop)
{
  if (IsRunning()) {
    Post(MoviePlayerCore::MSG_SET_LOOP, loop);
    Post(MoviePlayerCore::MSG_START);
    mEventFlag.Wait(EVENT_FLAG_PLAY_READY);
  }
}

void
MoviePlayerCore::Stop()
{
  if (IsRunning()) {
    Post(MoviePlayerCore::MSG_STOP);
    mEventFlag.Wait(EVENT_FLAG_STOPPED);
  }
}

void
MoviePlayerCore::Pause()
{
  if (IsRunning()) {
    Post(MoviePlayerCore::MSG_PAUSE);
  }
}

void
MoviePlayerCore::Resume()
{
  if (IsRunning()) {
    Post(MoviePlayerCore::MSG_RESUME);
  }
}

void
MoviePlayerCore::Seek(int64_t posUs)
{
  if (IsRunning()) {
    Post(MoviePlayerCore::MSG_SEEK, posUs);
  }
}

void
MoviePlayerCore::SetLoop(bool loop)
{
  if (IsRunning()) {
    Post(MoviePlayerCore::MSG_SET_LOOP, loop);
  }
}

void
MoviePlayerCore::SetPixelFormat(PixelFormat format)
{
  mPixelFormat = format;
}

bool
MoviePlayerCore::IsVideoAvailable() const
{
  return (mVideoDecoder != nullptr);
}

int32_t
MoviePlayerCore::Width() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return mWidth;
}

int32_t
MoviePlayerCore::Height() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return mHeight;
}

float
MoviePlayerCore::FrameRate() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return mFrameRate;
}

bool
MoviePlayerCore::IsAudioAvailable() const
{
  return (mAudioDecoder != nullptr);
}

int32_t
MoviePlayerCore::SampleRate() const
{
  if (IsAudioAvailable()) {
    return mAudioDecoder->SampleRate();
  } else {
    return -1;
  }
}

int32_t
MoviePlayerCore::Channels() const
{
  if (IsAudioAvailable()) {
    return mAudioDecoder->Channels();
  } else {
    return -1;
  }
}

int32_t
MoviePlayerCore::BitsPerSample() const
{
  if (IsAudioAvailable()) {
    return mAudioDecoder->BitsPerSample();
  } else {
    return -1;
  }
}

int32_t
MoviePlayerCore::Encoding() const
{
  if (IsAudioAvailable()) {
    return mAudioDecoder->Encoding();
  } else {
    return -1;
  }
}

int64_t
MoviePlayerCore::Duration() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return mClock.GetDuration();
}

int64_t
MoviePlayerCore::Position() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return mClock.GetCurrentMediaTime();
}

bool
MoviePlayerCore::IsPlaying() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return (mState == STATE_PLAY || mState == STATE_PAUSE || mState == STATE_PRELOADING);
}

bool
MoviePlayerCore::Loop() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return mIsLoop;
}

void
MoviePlayerCore::SelectTargetTrack()
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  size_t trackNum = mExtractor->GetTrackCount();
  for (size_t i = 0; i < trackNum; i++) {
    TrackInfo info;
    mExtractor->GetTrackInfo(i, &info);
    // LOGV("TRACK[%d]: type=%d, codec=%d\n", i, info.type, info.codecId);

    if (info.codecId == CODEC_UNKNOWN) {
      LOGV(" *** unknown codec! ignore track #%d ***\n", (int)i);
      continue;
    }

    switch (info.type) {
    case TRACK_TYPE_VIDEO: {
      LOGV(" VIDEO: width=%d, height=%d, fps=%f\n", info.v.width, info.v.height,
           info.v.frameRate);

      if (mVideoDecoder != nullptr) {
        // 複数ビデオトラックの場合は最初のトラックのみ対象とする
        continue;
      }

      if (info.codecId == CODEC_UNKNOWN) {
        LOGV("unsupported video codec! skip this track: index=%d\n", (int)i);
        continue;
      }

      mWidth     = info.v.width;
      mHeight    = info.v.height;
      mFrameRate = info.v.frameRate;

      mVideoDecoder = (VideoDecoder *)Decoder::CreateDecoder(info.codecId);
      ASSERT(mVideoDecoder != nullptr, "failed to create video decoder\n");

      InitDummyFrame();

      if (info.codecId == CODEC_V_VP8 || info.codecId == CODEC_V_VP9) {
        Decoder::Config config;
        config.Init(info.codecId);
        config.vpx.decCfg.w = info.v.width;
        config.vpx.decCfg.h = info.v.height;
        // libvpxのスレッド数を、ncpus-4 と 2 の大きい方を指定する
        // 5コア以上なら4スレッド、4コア以下なら2スレッド。
        // テストでは1080pくらいなら4スレッド以上は特に変わらない感じ。
        config.vpx.decCfg.threads = get_num_of_cpus() > 4 ? 4 : 2;
        // rgbFormat を指定するとdecoder 内部で変換まで終わらせる
        // その代わり MoviePlayerCore::RenderFrame() で指定したフォーマットは
        // 無視されるようになる。デコーダスレッド上でyuv/rgb変換が行われるので
        // メインループのCPU時間をコスト高のyuv変換で消費しなくなるメリットがある
        config.vpx.rgbFormat = mPixelFormat;
        config.vpx.alphaMode = info.v.alphaMode;
        mVideoDecoder->Configure(config);
      }

      mExtractor->SelectTrack(TRACK_TYPE_VIDEO, i);

    } break;
    case TRACK_TYPE_AUDIO: {
      LOGV(" AUDIO: channels=%d, sampleRate=%f, depth=%d\n", info.a.channels,
           info.a.sampleRate, info.a.bitDepth);

      if (mAudioDecoder != nullptr) {
        // 複数オーディオトラックの場合は最初のトラックのみ対象とする
        continue;
      }

      if (info.codecId == CODEC_UNKNOWN) {
        LOGV("unsupported audio codec! skip this track: index=%d\n", (int)i);
        continue;
      }

      mAudioDecoder = (AudioDecoder *)Decoder::CreateDecoder(info.codecId);
      ASSERT(mAudioDecoder != nullptr, "failed to create audio decoder\n");

      Decoder::Config config;
      config.Init(info.codecId);
      if (info.codecId == CODEC_A_VORBIS) {
        config.vorbis.channels   = info.a.channels;
        config.vorbis.sampleRate = info.a.sampleRate;
      } else if (info.codecId == CODEC_A_OPUS) {
        config.opus.channels   = info.a.channels;
        config.opus.sampleRate = info.a.sampleRate;
      }
      mExtractor->GetCodecPrivateData(i, config.privateData);
      mAudioDecoder->Configure(config);

      // AudioEngineを作成する
      // TODO AUDIO_FORMAT_S16 で決め打ちしておくが、汎用にするかどうか
      if (mAudioEngine != nullptr) {
        mAudioEngine->Stop();
        delete mAudioEngine;
      }
      AudioFormat audioFormat = AUDIO_FORMAT_S16;
      mAudioEngine            = new AudioEngine();
      mAudioEngine->Init(this, audioFormat, info.a.channels, info.a.sampleRate);

      switch (audioFormat) {
      case AUDIO_FORMAT_U8:
        mAudioUnitSize = info.a.channels * 1;
        break;
      case AUDIO_FORMAT_S16:
        mAudioUnitSize = info.a.channels * 2;
        break;
      case AUDIO_FORMAT_S32:
      case AUDIO_FORMAT_F32:
        mAudioUnitSize = info.a.channels * 4;
        break;
      }

      mExtractor->SelectTrack(TRACK_TYPE_AUDIO, i);

    } break;
    default: // ignore
      break;
    }
  }
}

bool
MoviePlayerCore::Open(const char *filepath)
{
  mExtractor   = new WebmExtractor();
  bool success = mExtractor->Open(filepath);
  if (!success) {
    LOGV("failed to create Extractor\n");
    return false;
  }

  mClock.SetDuration(mExtractor->GetDurationUs());

  SelectTargetTrack();
  Start();

  // Play() がかかるまでプリロードする
  PreLoadInput();

  return true;
}

void
MoviePlayerCore::DemuxInput()
{
  if (mSawInputEOS) {
    return;
  }

  bool isPreloading  = CurrentStateIs(STATE_PRELOADING);
  bool isInputFilled = false;
  bool reachedEOS    = false;

  do {
    Decoder *decoder = nullptr;
    TrackType type   = mExtractor->NextFramePacketType();
    switch (type) {
    default:
    case TRACK_TYPE_VIDEO:
      decoder = mVideoDecoder;
      // Vトラックあるのにデコーダが居ないのはバグなので捕まえておく
      ASSERT(decoder != nullptr, "BUG: invali video decoder.\n");
      break;
    case TRACK_TYPE_AUDIO:
      decoder = mAudioDecoder;
      // Aトラックあるのにデコーダが居ないのはバグなので捕まえておく
      ASSERT(decoder != nullptr, "BUG: invali audio decoder.\n");
      break;
    }

    // Extractor から Decoder への注入
    int32_t packetIndex = decoder->DequeueFramePacketIndex();
    if (packetIndex >= 0) {
      // LOGV("*** Decode - deq write buf index = %d\n", packetIndex);
      FramePacket *packet = decoder->GetFramePacket(packetIndex);
      mExtractor->ReadSampleData(packet);
      mSawInputEOS = packet->isEndOfStream;
      decoder->QueueFramePacketIndex(packetIndex);
      // LOGV("*** Decode - enq read buf index = %d\n", packetIndex);
      mExtractor->Advance();
    } else {
      // V/A考慮していないが、多分Vの入力が埋まって入力プリロードは終了
      isInputFilled = true;
    }
  } while (isPreloading && !isInputFilled);
}

void
MoviePlayerCore::HandleVideoOutput()
{
  if (!mVideoDecoder) {
    return;
  }

  if (mSawOutputEOS) {
    return;
  }

  bool isPreloading     = CurrentStateIs(STATE_PRELOADING);
  bool isFrameReady     = false;
  bool isFirstOfPreload = isPreloading;
  bool isFrameSkipping  = false;
  int32_t dcBufIndex    = -1;

  do {
    // 次フレームが未セットならデコード出力を吸い上げ
    if (mDecodedFrameNext == nullptr) {
      dcBufIndex = mVideoDecoder->DequeueDecodedBufferIndex();
      if (dcBufIndex >= 0) {
        DecodedBuffer *buf = mVideoDecoder->GetDecodedBuffer(dcBufIndex);
        mSawOutputEOS |= buf->isEndOfStream;
        if (!buf->isEndOfStream) {
          if (buf->dataSize > 0) {
            UpdateDecodedFrameNext(buf);
          }
        }
      } else if (isFrameSkipping) {
        // フレームスキップ解消中にデコード結果を吸い上げきった
        isFrameSkipping = false;
      }
    }

    // 次フレームに入れ替えるかチェック
    if (mDecodedFrameNext) {
      isFrameSkipping = false;

      int64_t timeDiff   = -1;
      int64_t nextTimeUs = ns_to_us(mDecodedFrameNext->timeStampNs);
      if (isFirstOfPreload) {
        // プリロード時の初回は強制的にDecodedFrame更新
        UpdateDecodedFrame(nullptr);
        isFirstOfPreload = false;
        isFrameReady     = true;
      } else if (mClock.IsStarted()) {
        if (IsAudioAvailable()) {
          timeDiff = mClock.CalcDiffFromMediaTime(nextTimeUs);
        } else {
          timeDiff = mClock.CalcDiffFromSystemTime(nextTimeUs);
        }
        // LOGV("frame diff time=%lld\n", timeDiff);

        // フレームスキップ判断のスレッショルド: 1/2フレーム遅れたら飛ばす
        int64_t frameSkipThresh = s_to_us(1 / mFrameRate / 2);
        if (timeDiff >= frameSkipThresh) {
          // フレームスキップ
          LOGV("*** video frame skipped: pts=%lld media=%lld sys=%lldus ***\n",
               nextTimeUs, mClock.GetCurrentMediaTime(), mClock.GetCurrentSystemTime());
          UpdateDecodedFrameNext(nullptr);
          isFrameSkipping = true;
        } else if (timeDiff >= 0) {
          // 出力ビデオフレーム更新
          UpdateDecodedFrame(nullptr);
          isFrameReady = true;
        }
      }
    }

  } while ((isPreloading && !isFrameReady) || isFrameSkipping);
}

void
MoviePlayerCore::HandleAudioOutput()
{
  if (!mAudioDecoder) {
    return;
  }

  if (mSawOutputEOS) {
    return;
  }

  bool isPreloading  = CurrentStateIs(STATE_PRELOADING);
  bool isFrameReady  = false;
  int32_t dcBufIndex = -1;

  do {
    dcBufIndex = mAudioDecoder->DequeueDecodedBufferIndex();
    if (dcBufIndex >= 0) {
      DecodedBuffer *buf = mAudioDecoder->GetDecodedBuffer(dcBufIndex);
      mSawOutputEOS |= buf->isEndOfStream;
      if (!buf->isEndOfStream) {
        if (buf->dataSize > 0) {
          EnqueueAudio(buf);
          isFrameReady = true;
        }
      }
    }
  } while (isPreloading && !isFrameReady);
}

void
MoviePlayerCore::Decode()
{
  DemuxInput();
  HandleVideoOutput();
  HandleAudioOutput();

  if (CurrentStateIs(STATE_PRELOADING)) {
    return;
  }

  bool isMovieDone       = false;
  bool isDecodeCompleted = mSawInputEOS && mSawOutputEOS;
  if (isDecodeCompleted) {
    // ラストフレームの持続時間をケア
    // リアルタイムクロックを参照して、Durationを超えていればdone処理をする
    // - 最後のリアルタイムオーディオ時間(あるいはビデオ時間)と現在の時間差
    // - 動画のDuration - 最後のフレームのPTS

    // seekした場合もケアする必要がある。startTimeはあてにできない？
  }

  if (isDecodeCompleted) {

#if 0 // TODO sleepではなくてyiedして空ループで消化する感じで
    // 最後のフレームは(Duration-現フレームPTS)分だけ残す
    int64_t postDelay = mClock.GetDuration() - mClock.GetCurrentMediaTime();
    // LOGV("render post delay sleep: %lld us \n", postDelay);
    std::this_thread::sleep_for(std::chrono::microseconds(postDelay));
#endif

    if (mIsLoop) {
      LOGV("---- Loop ----\n");
      Post(MSG_SEEK, 0);
      // Post(MSG_PRELOAD);
      Post(MSG_DECODE);
    } else {
      Post(MSG_STOP);
    }
  } else {
    Post(MSG_DECODE);
  }
}

void
MoviePlayerCore::HandleMessage(int32_t what, int64_t arg, void *data)
{
  // LOGV("handle msg %d\n", what);
  switch (what) {
  case MSG_PRELOAD:
    SetState(STATE_PRELOADING);
    Decode();
    mEventFlag.Set(EVENT_FLAG_PRELOADED);
    break;

  case MSG_START:
    SetState(STATE_PLAY);
    Decode();
    mEventFlag.Set(EVENT_FLAG_PLAY_READY);
    break;

  case MSG_DECODE:
    Decode();
    break;

  case MSG_PAUSE:
    if (GetState() == STATE_PLAY) {
      // 発行済のメッセージを全フラッシュ
      // その後MSG_DECODEを発行しないので、そのままデコード処理はポーズする
      SetState(STATE_PAUSE);
      Post(MSG_NOP, 0, nullptr, true);
    }
    break;

  case MSG_RESUME:
    if (GetState() == STATE_PAUSE) {
      SetState(STATE_PLAY);
      Post(MSG_DECODE);
    }
    break;

  case MSG_SET_LOOP: {
    std::lock_guard<std::mutex> lock(mApiMutex);
    mIsLoop = (arg != 0);
  } break;

  case MSG_SEEK: {
    Flush();
    mExtractor->SeekTo(arg);
    State savedState = GetState();
    SetState(STATE_PRELOADING);
    Decode();
    SetState(savedState);
  } break;

  case MSG_STOP:
    UpdateDecodedFrame(&mDummyFrame);
    SetState(STATE_STOP);
    Post(MSG_NOP, 0, nullptr, true); // flush msg
    mEventFlag.Set(EVENT_FLAG_STOPPED);
    break;

  case MSG_NOP: // no operation
    break;

  default:
    ASSERT(false, "unknown message type: %d\n", what);
    break;
  }

  std::this_thread::yield();
}

void
MoviePlayerCore::Flush()
{
  // ビデオ出力フレームをフラッシュ
  // 現在のフレームの内容をダミーフレームにコピー(bufIndexはコピーしない)
  // BufferQueueの造りの関係上、Flush()したときに現在mDecodedFrameで
  // 参照しているフレームだけ残してflushみたいなことが大変なのでこのように対応。
  // あるいは mDecodedFrameを毎回ポインタではなくコピー保持にする方法でも良いが
  // 現状ではポインタ保持なのでFlushの絡むseek処理のみこのように対応している。
  // (実際の所スレッド絡みなので毎回コピー保持したほうが丸いので、将来的にはそうなるかも)
  mDummyFrame.CopyFrom(mDecodedFrame, false);
  UpdateDecodedFrame(&mDummyFrame);
  UpdateDecodedFrameNext(nullptr);

  // オーディオ出力待ちキューをフラッシュ＆ステータス類をリセット
  if (mAudioDecoder != nullptr) {
    std::lock_guard<std::mutex> lock(mAudioFrameMutex);
    while (!mAudioFrameQueue.empty()) {
      DecodedBuffer *buf = mAudioFrameQueue.front();
      mAudioFrameQueue.pop();
      mAudioDecoder->ReleaseDecodedBufferIndex(buf->bufIndex);
    }
    mAudioQueuedBytes = 0;
    mAudioDataPos     = 0;
    mAudioTime.Reset();
  }

  // デコーダ類をフラッシュ
  if (mVideoDecoder != nullptr) {
    mVideoDecoder->FlushSync();
  }
  if (mAudioDecoder != nullptr) {
    mAudioDecoder->FlushSync();
  }

  mClock.Reset();

  mSawInputEOS  = false;
  mSawOutputEOS = false;
}

void
MoviePlayerCore::UpdateDecodedFrame(DecodedBuffer *newFrame)
{
  if (mVideoDecoder) {
    std::lock_guard<std::mutex> lock(mDecodedFrameMutex);

    // newFrameを明示的に指定しない場合は、mDecodedFrameNextをスライドする
    if (newFrame == nullptr) {
      newFrame          = mDecodedFrameNext;
      mDecodedFrameNext = nullptr;
    }
    DecodedBuffer *prevFrame = mDecodedFrame;
    mDecodedFrame            = newFrame;

    // LOGV("new video frame: pts=%lld us\n", ns_to_us(mDecodedFrame->timeStampNs));

    // 前フレームがダミーフレームでなければ開放
    if (prevFrame != &mDummyFrame) {
      mVideoDecoder->ReleaseDecodedBufferIndex(prevFrame->bufIndex);
    }

    // 新フレームがダミーフレームではなく、
    // Audioトラックがない場合は、メディアタイムをビデオフレームのPTSで更新
    if (!IsAudioAvailable() && newFrame != &mDummyFrame) {
      int64_t mediaTimeNs = ns_to_us(mDecodedFrame->timeStampNs);
      mClock.SetCurrentMediaTime(ns_to_us(mediaTimeNs));
      if (!mClock.IsStarted()) {
        mClock.SetStartTime(mediaTimeNs);
      }
    }
  }
}

void
MoviePlayerCore::UpdateDecodedFrameNext(DecodedBuffer *nextFrame)
{
  if (mVideoDecoder) {
    std::lock_guard<std::mutex> lock(mDecodedFrameMutex);

    // フレームスキップなどで破棄するケースではnextがnon-nullで呼ばれるので
    // その場合は先に現在のnextを開放する
    if (mDecodedFrameNext != nullptr) {
      mVideoDecoder->ReleaseDecodedBufferIndex(mDecodedFrameNext->bufIndex);
      mDecodedFrameNext = nullptr;
    }
    mDecodedFrameNext = nextFrame;
  }
}

const DecodedBuffer *
MoviePlayerCore::GetDecodedFrame() const
{
  std::lock_guard<std::mutex> lock(mDecodedFrameMutex);

  return mDecodedFrame;
}

bool
MoviePlayerCore::GetAudioFrame(uint8_t *frames, int64_t frameCount, uint64_t *framesRead,
                               uint64_t *timeStampUs)
{
  bool hasNewFrame    = false;
  uint64_t readFrames = 0;
  uint64_t mediaTime  = 0;

  // プリロード中は出力を行わない
  if (GetState() == STATE_PRELOADING) {
    readFrames  = 0;
    hasNewFrame = false;
    goto out;
  }

  if (IsAudioAvailable()) {
    std::lock_guard<std::mutex> lock(mAudioFrameMutex);

    // 要求を満たす量の出力がない
    uint64_t reqBytes = frameCount * mAudioUnitSize;
    if (mAudioQueuedBytes < reqBytes) {
      readFrames  = 0;
      hasNewFrame = false;
      goto out;
    }

    uint8_t *dst          = frames;
    uint64_t bytesToRead  = reqBytes;
    uint64_t timeBase     = mAudioTime.base;
    uint64_t timeOffset   = mAudioTime.offset;
    uint64_t nextTimeBase = 0;

    bool first = true;
    while (bytesToRead > 0 && mAudioFrameQueue.size() > 0) {
      const DecodedBuffer *data = mAudioFrameQueue.front();

      if (first) {
        // case1 前回がバッファをちょうど使い切って今回新バッファから
        // case2 seekで飛んだ
        // どちらにしても状況的にオフセットは先頭から始まるので常に0。
        if (timeBase != data->timeStampNs) {
          timeBase   = data->timeStampNs;
          timeOffset = 0;
        }
        first = false;
      }
      nextTimeBase = data->timeStampNs;

      int remain = data->dataSize - mAudioDataPos;
      if (bytesToRead < remain) {
        memcpy(dst, data->data + mAudioDataPos, bytesToRead);
        dst += bytesToRead;
        readFrames += (bytesToRead / mAudioUnitSize);
        mAudioDataPos += bytesToRead;
        bytesToRead = 0;
      } else {
        memcpy(dst, data->data + mAudioDataPos, remain);
        mAudioFrameQueue.pop();
        mAudioDecoder->ReleaseDecodedBufferIndex(data->bufIndex);
        dst += remain;
        readFrames += (remain / mAudioUnitSize);
        mAudioDataPos = 0;
        bytesToRead -= remain;
      }
    }
    mAudioQueuedBytes -= reqBytes;
    hasNewFrame = true;

    // LOGV("readed: %lld, pos: %lld\n", reqBytes, pos);

    mediaTime = ns_to_us(timeBase + timeOffset);
    mClock.SetCurrentMediaTime(mediaTime);
    if (!mClock.IsStarted()) {
      mClock.SetStartTime(mediaTime);
    }

    mAudioTime.base = nextTimeBase;
    mAudioTime.offset =
      mAudioDataPos * 1'000'000'000 / (mAudioDecoder->SampleRate() * mAudioUnitSize);

#if 0 // DEBUG
    LOGV("time=%lld, base=%lld, offset=%lld\n", timeBase + timeOffset, timeBase,
         timeOffset);
    LOGV("next: time=%lld, base=%lld, offset=%lld\n", mAudioTime.base + mAudioTime.offset,
         mAudioTime.base, mAudioTime.offset);
#endif
  } else {
    // 音声トラックがない
    readFrames  = 0;
    hasNewFrame = false;
  }

out:
  if (framesRead) {
    *framesRead = readFrames;
  }

  if (timeStampUs) {
    *timeStampUs = mediaTime;
  }

  return hasNewFrame;
}

void
MoviePlayerCore::PreLoadInput()
{
  SetState(STATE_PRELOADING);
  Post(MSG_PRELOAD);
  mEventFlag.Wait(EVENT_FLAG_PRELOADED);
}

void
MoviePlayerCore::SetState(State newState)
{
  if (mAudioEngine) {
    switch (newState) {
    case STATE_PLAY:
      // mClock.SetStartTime();
      mAudioEngine->Start();
      break;
    case STATE_PAUSE:
    case STATE_STOP:
      mAudioEngine->Stop();
      break;
    default:
      // nothing to do.
      break;
    }
  }

  mState = newState;
}

MoviePlayerCore::State
MoviePlayerCore::GetState() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return mState;
}

bool
MoviePlayerCore::CurrentStateIs(State state) const
{
  return GetState() == state;
}
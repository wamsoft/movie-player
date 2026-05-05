#define MYLOG_TAG "MoviePlayerCore"
#include "BasicLog.h"
#include "MoviePlayerCore.h"

#include "IAudioSink.h"
#include "IMoviePlayer.h"

MoviePlayerCore::MoviePlayerCore(PixelFormat pixelFormat, IAudioSink *audioSink)
: mState(STATE_UNINIT)
, mPixelFormat(pixelFormat)
, mAudioSink(audioSink)
, mOnStateFunc(nullptr)
, mOnVideoDecodedFunc(nullptr)
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
  mWidth             = -1;
  mHeight            = -1;
  mOutputPixelFormat = PIXEL_FORMAT_UNKNOWN;

  mSawVideoInputEOS  = false;
  mSawAudioInputEOS  = false;
  mSawVideoOutputEOS = false;
  mSawAudioOutputEOS = false;

  mLastVideoFrameEnd = false;
  mLastAudioFrameEnd = false;

  mIsLoop = false;

  mExtractor    = nullptr;
  mVideoDecoder = nullptr;
  mAudioDecoder = nullptr;

  mClock.Reset();

  mVideoFrame = mVideoFrameNext = nullptr;
  mVideoFrameLastGet            = nullptr;
  mDummyFrame.InitByType(TRACK_TYPE_VIDEO, -1);

  mAudioUnitSize          = 0;
  mAudioCodecDelayUs      = 0;
  mAudioStartPtsNs        = 0;
  mAudioStartPtsValid     = false;
  mAudioResumeMediaTimeUs = 0;
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
      std::lock_guard<std::mutex> lock(mVideoFrameMutex);

      mVideoFrame = &mDummyFrame;
      EnqueueVideo(mVideoFrame);
    }
  }
}

void
MoviePlayerCore::Done()
{
  Flush();
  StopThread();

  // mAudioSink は host 所有なので touch しない (ここでは Stop も含めない;
  // host 側が VideoOverlay close 時に責任を持つ)

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

  mOnStateFunc        = nullptr;
  mOnVideoDecodedFunc = nullptr;
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

PixelFormat
MoviePlayerCore::OutputPixelFormat() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return mOutputPixelFormat;
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

  return mClock.GetPresentationTime();
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
        config.vpx.rgbFormat      = mPixelFormat;
        config.vpx.alphaMode      = info.v.alphaMode;
        mVideoDecoder->Configure(config);

        mOutputPixelFormat = mVideoDecoder->OutputPixelFormat();
      }

      mExtractor->SelectTrack(TRACK_TYPE_VIDEO, i);

      if (mVideoDecoder) {
        LOGV(" VIDEO: codec=%s, width=%d, height=%d, fps=%f\n",
             mVideoDecoder->CodecName(), info.v.width, info.v.height, info.v.frameRate);
      }

    } break;
    case TRACK_TYPE_AUDIO: {
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

      mAudioCodecDelayUs = ns_to_us(info.a.codecDelay);

      // TODO AUDIO_FORMAT_S16 で固定。汎用にするならインタフェース追加
      AudioFormat audioFormat = AUDIO_FORMAT_S16;
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

      // 外部 audio sink にフォーマットを通知。失敗したら audio 無し再生に切替。
      if (mAudioSink != nullptr) {
        IAudioSink::Encoding encoding = IAudioSink::PCM_S16;
        switch (audioFormat) {
        case AUDIO_FORMAT_U8:  encoding = IAudioSink::PCM_U8;  break;
        case AUDIO_FORMAT_S16: encoding = IAudioSink::PCM_S16; break;
        case AUDIO_FORMAT_S32: encoding = IAudioSink::PCM_S32; break;
        case AUDIO_FORMAT_F32: encoding = IAudioSink::PCM_F32; break;
        default: break;
        }
        int32_t bitsPerSample = mAudioUnitSize * 8 / info.a.channels;
        if (!mAudioSink->Setup(info.a.channels, (int)info.a.sampleRate,
                               bitsPerSample, encoding)) {
          LOGE("audio sink setup failed; disabling audio output\n");
          mAudioSink = nullptr;
        }
      }

      mExtractor->SelectTrack(TRACK_TYPE_AUDIO, i);

      if (mAudioDecoder) {
        LOGV(" AUDIO: codec=%s, channels=%d, sampleRate=%f, depth=%d, codecDelay=%" PRIu64
             "\n",
             mAudioDecoder->CodecName(), info.a.channels, info.a.sampleRate,
             info.a.bitDepth, mAudioCodecDelayUs);
      }

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
  OpenSetup();
  return true;
}

bool
MoviePlayerCore::Open(IMovieReadStream *stream)
{
  mExtractor   = new WebmExtractor();
  bool success = mExtractor->Open(stream);
  if (!success) {
    LOGV("failed to create Extractor\n");
    return false;
  }
  OpenSetup();
  return true;
}

void
MoviePlayerCore::OpenSetup()
{
  mClock.SetDuration(mExtractor->GetDurationUs());

  SelectTargetTrack();
  InitStatusFlags();
  Start();

  // Play() がかかるまでプリロードする
  PreLoadInput();
}

void
MoviePlayerCore::InitStatusFlags()
{
  mSawVideoInputEOS = mSawVideoOutputEOS = mLastVideoFrameEnd = !IsVideoAvailable();
  mSawAudioInputEOS = mSawAudioOutputEOS = mLastAudioFrameEnd = !IsAudioAvailable();
}

void
MoviePlayerCore::DemuxInput()
{
  if (mSawVideoInputEOS && mSawAudioInputEOS) {
    return;
  }

  bool isPreloading  = IsCurrentState(STATE_PRELOADING);
  bool isInputFilled = false;
  bool reachedEOS    = false;

  do {
    Decoder *decoder = nullptr;
    TrackType type   = mExtractor->NextFramePacketType();
    switch (type) {
    case TRACK_TYPE_VIDEO:
      decoder = mVideoDecoder;
      ASSERT(decoder != nullptr, "BUG: invali video decoder.\n");
      break;
    case TRACK_TYPE_AUDIO:
      decoder = mAudioDecoder;
      ASSERT(decoder != nullptr, "BUG: invali audio decoder.\n");
      break;
    case TRACK_TYPE_UNKNOWN:
    default:
      break;
    }

    int32_t packetIndex = -1;
    if (decoder && !mExtractor->IsReachedEOS()) {
      packetIndex = InputToDecoder(decoder, false);
      if (packetIndex < 0) {
        isInputFilled = true; // 入力プリロード終了
      }
    }

    // 入力パケットを生成してEOSフラグを設定
    if (mExtractor->IsReachedEOS()) {
      if (IsVideoAvailable() && !mSawVideoInputEOS) {
        // LOGV("video EOS\n");
        packetIndex = InputToDecoder(mVideoDecoder, true);
        if (packetIndex >= 0) {
          mSawVideoInputEOS = true;
        }
      }
      if (IsAudioAvailable() && !mSawAudioInputEOS) {
        // LOGV("audio EOS\n");
        packetIndex = InputToDecoder(mAudioDecoder, true);
        if (packetIndex >= 0) {
          mSawAudioInputEOS = true;
        }
      }
    }
  } while (isPreloading && !isInputFilled);
}

int32_t
MoviePlayerCore::InputToDecoder(Decoder *decoder, bool inputIsEOS)
{
  int32_t packetIndex = decoder->DequeueFramePacketIndex();
  if (packetIndex >= 0) {
    FramePacket *packet = decoder->GetFramePacket(packetIndex);
    if (inputIsEOS) {
      packet->InitAsEOS();
    } else {
      mExtractor->ReadSampleData(packet);
      mExtractor->Advance();
    }
    decoder->QueueFramePacketIndex(packetIndex);
  }

  return packetIndex;
}

void
MoviePlayerCore::HandleVideoOutput()
{
  if (!mVideoDecoder) {
    return;
  }

  bool isPreloading     = IsCurrentState(STATE_PRELOADING);
  bool isFrameReady     = false;
  bool isFirstOfPreload = isPreloading;
  bool isFrameSkipping  = false;
  int32_t dcBufIndex    = -1;

  do {
    // 次フレームが未セットならデコード出力を吸い上げ
    if (!mSawVideoOutputEOS && mVideoFrameNext == nullptr) {
      dcBufIndex = mVideoDecoder->DequeueDecodedBufferIndex();
      if (dcBufIndex >= 0) {
        DecodedBuffer *buf = mVideoDecoder->GetDecodedBuffer(dcBufIndex);
        mSawVideoOutputEOS = buf->isEndOfStream;
        if (!buf->isEndOfStream) {
          if (buf->dataSize > 0) {
            SetVideoFrameNext(buf);
          }
        } else {
          // LOGV("output EOS: video\n");
        }
      } else if (isFrameSkipping) {
        // フレームスキップ解消中にデコード結果を吸い上げきった
        isFrameSkipping = false;
        std::this_thread::yield();
      }
    }

    // 次フレームに入れ替えるかチェック
    if (mVideoFrameNext) {
      isFrameSkipping = false;

      if (isFirstOfPreload) {
        // プリロード時の初回は強制的にDecodedFrame更新
        UpdateVideoFrameToNext();
        isFirstOfPreload = false;
        isFrameReady     = true;
      } else if (mClock.IsStarted()) {
        // フレームスキップ判断のスレッショルド: 1/2フレーム遅れたら飛ばす
        int64_t frameSkipThresh = s_to_us(1 / mFrameRate / 2);
        int64_t timeDiff        = CalcDiffVideoTimeAndNow(mVideoFrameNext);
        if (false && timeDiff >= frameSkipThresh) {
          // フレームスキップ
          LOGV("*** video frame skipped: frame=%" PRId64 ", pts=%" PRId64
               ", diff=%" PRId64 ", thresh=%" PRId64 "\n",
               mVideoFrameNext->frame, ns_to_us(mVideoFrameNext->timeStampNs), timeDiff,
               frameSkipThresh);
          SetVideoFrameNext(nullptr);
          isFrameSkipping = true;
        } else if (timeDiff >= 0) {
          // 出力ビデオフレーム更新
          // LOGV("video update: pts=%" PRId64 ", diff=%" PRId64 "\n",
          //      ns_to_us(mVideoFrameNext->timeStampNs), timeDiff);
          UpdateVideoFrameToNext();
          isFrameReady = true;
        }
      }
    }

    // デコード吸い上げが完了＆次フレームが空の状態ならば
    // 最終ビデオフレームなのでエンドタイムをチェックして
    // 終了フラグを立てる
    if (mSawVideoOutputEOS && mVideoFrameNext == nullptr) {
      // LOGV("video last frame\n");
      int64_t timeDiff = CalcDiffVideoTimeAndNow(mVideoFrame);
      if (timeDiff >= 0) {
        // LOGV("video last frame END\n");
        mLastVideoFrameEnd = true;
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

  // sink が再生完了通知を返してきた DecodedBuffer を引き取り、
  // 同時に sink->GetSamplesPlayed をベースに MediaClock を更新する。
  DrainAudioSinkConsumed();

  if (mSawAudioOutputEOS) {
    return;
  }

  bool isPreloading  = IsCurrentState(STATE_PRELOADING);
  bool isFrameReady  = false;
  int32_t dcBufIndex = -1;

  do {
    dcBufIndex = mAudioDecoder->DequeueDecodedBufferIndex();
    if (dcBufIndex >= 0) {
      DecodedBuffer *buf = mAudioDecoder->GetDecodedBuffer(dcBufIndex);
      mSawAudioOutputEOS = buf->isEndOfStream;
      if (buf->dataSize > 0 || buf->isEndOfStream) {
        // EOSバッファも終了確認マーカーとしてキューイングする
        EnqueueAudio(buf);
        isFrameReady = true;
      }
    } else {
      std::this_thread::yield();
    }
  } while (isPreloading && !isFrameReady);
}

void
MoviePlayerCore::Decode()
{
  DemuxInput();
  HandleVideoOutput();
  HandleAudioOutput();

  if (IsCurrentState(STATE_PRELOADING)) {
    return;
  }

  bool sawInputEOS  = mSawVideoInputEOS && mSawAudioInputEOS;
  bool sawOutputEOS = mSawVideoOutputEOS && mSawAudioOutputEOS;
  bool lastFrameEnd = mLastVideoFrameEnd && mLastAudioFrameEnd;

  bool isMovieDone = (sawInputEOS && sawOutputEOS && lastFrameEnd);
  if (isMovieDone) {
    if (mIsLoop) {
      LOGV("---- Loop ----\n");
      Post(MSG_SEEK, 0);
      Post(MSG_DECODE);
    } else {
      Post(MSG_FINISH);
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
    if (IsCurrentState(STATE_PLAY)) {
      // 発行済のメッセージを全フラッシュ
      // その後MSG_DECODEを発行しないので、そのままデコード処理はポーズする
      SetState(STATE_PAUSE);
      Post(MSG_NOP, 0, nullptr, true);
    }
    break;

  case MSG_RESUME:
    if (IsCurrentState(STATE_PAUSE)) {
      mClock.ClearStartMediaTime();

      if (IsAudioAvailable()) {
        // sink->GetSamplesPlayed の起点が変わるので start PTS を仕切り直す。
        // 次に enqueue されるバッファの PTS が新しい起点になる。
        mAudioStartPtsValid = false;

        // sink からのクロック更新が走る前に video が描画判定を走ら
        // せる可能性に備えて、前回の最終タイムで start/anchor を初期化。
        mClock.SetStartMediaTime(mAudioResumeMediaTimeUs);
        mClock.SetPresentationTime(mAudioResumeMediaTimeUs);
        mClock.UpdateAnchorTime(mAudioResumeMediaTimeUs, get_time_us(), INT64_MAX);
      }

      // レジューム直後はビデオを強制的に次フレームに更新
      if (IsVideoAvailable() && mVideoFrameNext) {
        UpdateVideoFrameToNext();
      }

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
    SetVideoFrame(&mDummyFrame);
    SetState(STATE_STOP);
    Post(MSG_NOP, 0, nullptr, true); // flush msg
    mEventFlag.Set(EVENT_FLAG_STOPPED);
    break;

  case MSG_FINISH:
    SetVideoFrame(&mDummyFrame);
    SetState(STATE_FINISH);
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
  if (mVideoFrame) {
    mDummyFrame.CopyFrom(mVideoFrame, false);
    SetVideoFrame(&mDummyFrame);
    SetVideoFrameNext(nullptr);
  }

  mVideoFrameLastGet = nullptr;

  // オーディオ出力待ちをフラッシュ
  if (mAudioDecoder != nullptr) {
    if (mAudioSink) {
      // pending を全て consumed に流して取り出す。
      mAudioSink->Flush();
      void *param = nullptr;
      while (mAudioSink->TryPopConsumed(&param)) {
        DecodedBuffer *buf = (DecodedBuffer *)param;
        if (buf) {
          mAudioDecoder->ReleaseDecodedBufferIndex(buf->bufIndex);
        }
      }
    }
    mAudioStartPtsValid     = false;
    mAudioStartPtsNs        = 0;
    mAudioResumeMediaTimeUs = 0;
  }

  // デコーダ類をフラッシュ
  if (mVideoDecoder != nullptr) {
    mVideoDecoder->FlushSync();
  }
  if (mAudioDecoder != nullptr) {
    mAudioDecoder->FlushSync();
  }

  mClock.ClearStartMediaTime();
  mClock.ClearAnchorTime();

  InitStatusFlags();

  mLastVideoFrameEnd = false;
}

void
MoviePlayerCore::UpdateVideoFrameToNext()
{
  if (mVideoDecoder) {
    // mDecodedFrameNextをmDecodedFrameへスライドする
    DecodedBuffer *newFrame = mVideoFrameNext;
    mVideoFrameNext         = nullptr;
    SetVideoFrame(newFrame);
  }
}

void
MoviePlayerCore::SetVideoFrame(DecodedBuffer *newFrame)
{
  if (mVideoDecoder) {
    std::lock_guard<std::mutex> lock(mVideoFrameMutex);

    DecodedBuffer *prevFrame = mVideoFrame;
    mVideoFrame              = newFrame;

    if (prevFrame == mVideoFrame) {
      // 前フレームと同じなら何もしない
      return;
    }

    EnqueueVideo(mVideoFrame);

    // LOGV("new video frame: pts=%" PRId64 " us\n", ns_to_us(mVideoFrame->timeStampNs));

    // 前フレームがダミーフレームでなければ開放
    if (prevFrame && prevFrame != &mDummyFrame) {
      mVideoDecoder->ReleaseDecodedBufferIndex(prevFrame->bufIndex);
    }

    // 新フレームがダミーフレームではなく、
    // Audioトラックがない場合は、メディアタイムをビデオフレームのPTSで更新
    if (!IsAudioAvailable() && newFrame != &mDummyFrame) {
      int64_t mediaTimeUs = ns_to_us(mVideoFrame->timeStampNs);
      if (!mClock.IsStarted()) {
        mClock.SetStartMediaTime(mediaTimeUs);
      }
      mClock.SetPresentationTime(mediaTimeUs);

      int64_t nowUs          = get_time_us();
      int64_t nowMediaUs     = mediaTimeUs;
      int64_t durationUs     = s_to_us(1.0 / mFrameRate);
      int64_t maxMediaTimeUs = mediaTimeUs + durationUs;
      mClock.UpdateAnchorTime(nowMediaUs, nowUs, maxMediaTimeUs);
    }
  }
}

void
MoviePlayerCore::SetVideoFrameNext(DecodedBuffer *nextFrame)
{
  if (mVideoDecoder) {
    std::lock_guard<std::mutex> lock(mVideoFrameMutex);

    // フレームスキップなどで破棄するケースではnextがnon-nullで呼ばれるので
    // その場合は先に現在のnextを開放する
    if (mVideoFrameNext != nullptr) {
      mVideoDecoder->ReleaseDecodedBufferIndex(mVideoFrameNext->bufIndex);
      mVideoFrameNext = nullptr;
    }
    mVideoFrameNext = nextFrame;
  }
}

bool
MoviePlayerCore::GetVideoFrame(const DecodedBuffer **videoFrame)
{
  std::lock_guard<std::mutex> lock(mVideoFrameMutex);

  // とりあえず常に返しておいて問題はないはずなので返しておく
  *videoFrame = mVideoFrame;

  if (mVideoFrameLastGet != mVideoFrame) {
    mVideoFrameLastGet = mVideoFrame;
    return true;
  } else {
    return false;
  }
}

int64_t
MoviePlayerCore::CalcDiffVideoTimeAndNow(DecodedBuffer *targetFrame) const
{
  if (!targetFrame) {
    return -1;
  }

  int64_t mediaUs    = ns_to_us(targetFrame->timeStampNs);
  int64_t nextRealUs = mClock.GetRealTimeFor(mediaUs);
  int64_t nowUs      = get_time_us();

  return nowUs - nextRealUs;
}

void
MoviePlayerCore::EnqueueAudio(DecodedBuffer *data)
{
  if (mAudioSink == nullptr) {
    // audio 無し再生。decoder buffer は即時 release。
    if (data->isEndOfStream) {
      mLastAudioFrameEnd = true;
    }
    if (mAudioDecoder) {
      mAudioDecoder->ReleaseDecodedBufferIndex(data->bufIndex);
    }
    return;
  }

  // sink にデータを流す。data ポインタは consumed 通知が返るまで有効である必要が
  // あり、DecodedBuffer 自体の寿命は decoder の管理下にある (ReleaseDecodedBufferIndex
  // を呼ぶまで安定)。consumed 時にここから ReleaseDecodedBufferIndex を呼ぶ。
  if (!mAudioStartPtsValid) {
    mAudioStartPtsNs    = data->timeStampNs;
    mAudioStartPtsValid = true;
  }
  mAudioSink->Enqueue(data->data, data->dataSize, data->isEndOfStream, data);
}

void
MoviePlayerCore::EnqueueVideo(DecodedBuffer *data)
{
  if (mOnVideoDecodedFunc) {
    if (data->v.width > 0 && data->v.height > 0) {
      // ビデオフレームのサイズが0x0でない場合はコールバックを呼ぶ
      mOnVideoDecodedFunc(data);
    } else {
      // サイズが0x0の場合は、コールバックは呼ばない
      LOGV("video frame size is 0x0, skip callback\n");
    }
  }
}

void
MoviePlayerCore::DrainAudioSinkConsumed()
{
  // sink が再生完了 (consumed) 通知した DecodedBuffer を引き取り、
  // ・decoder buffer を release
  // ・EOS マーカー検出で mLastAudioFrameEnd セット
  // を行う。本メソッドは decoder thread (Decode ループ) から呼ばれる。
  if (mAudioSink == nullptr || mAudioDecoder == nullptr) {
    return;
  }

  void *param = nullptr;
  while (mAudioSink->TryPopConsumed(&param)) {
    DecodedBuffer *buf = (DecodedBuffer *)param;
    if (!buf) continue;
    if (buf->isEndOfStream) {
      mLastAudioFrameEnd = true;
    }
    mAudioDecoder->ReleaseDecodedBufferIndex(buf->bufIndex);
  }

  // sink が報告する再生済み frame 数で MediaClock を anchor 更新する。
  // (起点 PTS = 最初に enqueue したバッファの PTS、線形仮定)
  if (!IsAudioAvailable() || !mAudioStartPtsValid || mLastAudioFrameEnd) {
    return;
  }
  if (IsCurrentState(STATE_PRELOADING)) {
    return;
  }
  int64_t sampleRate = mAudioDecoder->SampleRate();
  if (sampleRate <= 0) return;

  int64_t samplesPlayed = mAudioSink->GetSamplesPlayed();
  int64_t playedUs      = calc_audio_duration_us(samplesPlayed, sampleRate);
  int64_t mediaTimeUs   = ns_to_us(mAudioStartPtsNs) + playedUs - mAudioCodecDelayUs;
  if (mediaTimeUs < 0) {
    return;
  }
  if (!mClock.IsStarted()) {
    mClock.SetStartMediaTime(mediaTimeUs);
  }
  mClock.SetPresentationTime(mediaTimeUs);

  int64_t nowUs = get_time_us();
  // chunk 1 つぶんの先 (= 安全マージン) を maxMediaTimeUs として渡す
  int64_t maxMediaTimeUs = mediaTimeUs + 100000; // +100ms
  mClock.UpdateAnchorTime(mediaTimeUs, nowUs, maxMediaTimeUs);
  mAudioResumeMediaTimeUs = mediaTimeUs;
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
  if (mAudioSink) {
    switch (newState) {
    case STATE_PLAY:
      mAudioSink->Start();
      break;
    case STATE_PAUSE:
    case STATE_STOP:
    case STATE_FINISH:
      mAudioSink->Stop();
      break;
    default:
      // nothing to do.
      break;
    }
  }
  if (mState != newState) {
    mState = newState;
    if (mOnStateFunc) {
      mOnStateFunc(mState);
    }
  }
}

MoviePlayerCore::State
MoviePlayerCore::GetState() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return mState;
}

bool
MoviePlayerCore::IsCurrentState(State state) const
{
  return GetState() == state;
}

void
MoviePlayerCore::SetVolume(float volume)
{
  if (mAudioSink) {
    mAudioSink->SetVolume(volume);
  }
}

float
MoviePlayerCore::Volume() const
{
  float volume = 1.0f;
  if (mAudioSink) {
    volume = mAudioSink->Volume();
  }
  return volume;
}
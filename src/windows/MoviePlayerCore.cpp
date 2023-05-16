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

  mTimer.ClearStartTime();

  mDecodedFrame = mDecodedFrameNext = nullptr;
  mDummyFrame.InitByType(TRACK_TYPE_VIDEO, -1);

  mAudioEngine = nullptr;
}

void
MoviePlayerCore::InitDummyFrame()
{
  if (mVideoDecoder) {
    mDummyFrame.InitByType(TRACK_TYPE_VIDEO, -1);
    mDummyFrame.Resize(mWidth * mHeight * 4);
    mDummyFrame.timeStampNs = 0;
    mDummyFrame.frame = 0; // TODO -1だと無効フレーム扱いなので、ダミーフラグを付けるかなんか考える
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

  return mTimer.GetDuration();
}

int64_t
MoviePlayerCore::Position() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return mTimer.GetCurrentMediaTime();
}

bool
MoviePlayerCore::IsPlaying() const
{
  std::lock_guard<std::mutex> lock(mApiMutex);

  return (mState == STATE_PLAY || mState == STATE_PAUSE);
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
      LOGV(" VIDEO: width=%d, height=%d\n", info.v.width, info.v.height);

      if (mVideoDecoder != nullptr) {
        // 複数ビデオトラックの場合は最初のトラックのみ対象とする
        continue;
      }

      if (info.codecId == CODEC_UNKNOWN) {
        LOGV("unsupported video codec! skip this track: index=%d\n", (int)i);
        continue;
      }

      mWidth  = info.v.width;
      mHeight = info.v.height;

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
        // メインループのCPU時間をコスト高のyub変換で消費しなくなるメリットがある
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
        // ユースケース的に再利用は発生しないはずだが一応ケアしておく
        mAudioEngine->Stop();
        delete mAudioEngine;
      }
      mAudioEngine = new AudioEngine();
      mAudioEngine->Init(AUDIO_FORMAT_S16, info.a.channels, info.a.sampleRate);

      // TODO test
      mAudioEngine->mPlayer = this;

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

  mTimer.SetDuration(mExtractor->GetDurationUs());

  SelectTargetTrack();
  Start();

  // Play() がかかるまでプリロードする
  PreLoadInput();

  return true;
}

void
MoviePlayerCore::HandleInput(bool isPreloading)
{
  if (mSawInputEOS) {
    return;
  }

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
MoviePlayerCore::HandleOutput(bool isPreloading)
{
  if (mSawOutputEOS) {
    return;
  }

  bool isOutputReady    = false;
  bool isFirstOfPreload = isPreloading;
  int32_t dcBufIndex    = -1;

  do {
    if (mVideoDecoder != nullptr) {
      // 次フレームが未セットならデコード出力を吸い上げ
      if (mDecodedFrameNext == nullptr) {
        dcBufIndex = mVideoDecoder->DequeueDecodedBufferIndex();
        if (dcBufIndex >= 0) {
          DecodedBuffer *buf = mVideoDecoder->GetDecodedBuffer(dcBufIndex);
          mSawOutputEOS |= buf->isEndOfStream;
          if (!buf->isEndOfStream) {
            if (buf->dataSize > 0) {
              UpdateDecodedFrameNext(buf);
              isOutputReady = true;
            }
          }
        }
      }

      // 次フレームに入れ替えるかチェック
      if (mDecodedFrameNext) {
        int64_t timeDiff   = -1;
        int64_t nextTimeUs = ns_to_us(mDecodedFrameNext->timeStampNs);
        if (isFirstOfPreload) {
          // プリロード時の初回は強制的にDecodedFrame更新
          UpdateDecodedFrame(nullptr);
          isFirstOfPreload = false;
        } else if (mTimer.IsStarted()) {
          timeDiff = mTimer.CalcDiffFromMediaTime(nextTimeUs);
          timeDiff = mTimer.CalcDiffFromSystemTime(nextTimeUs); // TODO test
          // LOGV("frame diff time=%lld\n", timeDiff);

          // TODO フレームスキップ
          // UpdateDecodedFrameNext(nullptr);

          if (timeDiff >= 0) {
            UpdateDecodedFrame(nullptr);
          }
        }
      }
    }

    if (mAudioDecoder != nullptr) {
      dcBufIndex = mAudioDecoder->DequeueDecodedBufferIndex();
      if (dcBufIndex >= 0) {
        DecodedBuffer *buf = mAudioDecoder->GetDecodedBuffer(dcBufIndex);
        mSawOutputEOS |= buf->isEndOfStream;
        if (!buf->isEndOfStream) {
          if (buf->dataSize > 0) {
            // TODO GetAudio() を実装したらここはなくなる
            mAudioEngine->Enqueue(buf->data, buf->dataSize, false, buf->timeStampNs);
            if (!mVideoDecoder) {
              isOutputReady = true;
            }
          }
        }
        mAudioDecoder->ReleaseDecodedBufferIndex(dcBufIndex);
      }
    }
  } while (isPreloading && !isOutputReady);
}

void
MoviePlayerCore::Decode(bool oneShot)
{
  Decoder *decoder = nullptr;

  // プリロード制御
  bool isPreloading = (GetState() == STATE_PRELOADING || oneShot);

  HandleInput(isPreloading);
  HandleOutput(isPreloading);

  // ワンショットはメッセージチェインせず返る
  if (isPreloading) {
    return;
  }

  bool isDecodeCompleted = mSawInputEOS && mSawOutputEOS;
  if (isDecodeCompleted) {

#if 0 // TODO sleepではなくてyiedして空ループで消化する感じで
    // 最後のフレームは(Duration-現フレームPTS)分だけ残す
    int64_t postDelay = mTimer.GetDuration() - mTimer.GetCurrentMediaTime();
    // LOGV("render post delay sleep: %lld us \n", postDelay);
    std::this_thread::sleep_for(std::chrono::microseconds(postDelay));
#endif

    if (mIsLoop) {
      LOGV("---- Loop ----\n");
      Post(MSG_SEEK, 0);
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

  case MSG_SEEK:
    // 現在のフレームの内容をダミーフレームにコピー(bufIndexはコピーしない)
    // BufferQueueの造りの関係上、Flush()したときに現在mDecodedFrameで
    // 参照しているフレームだけ残してflushみたいなことが大変なのでこのように対応。
    // あるいは mDecodedFrameを毎回ポインタではなくコピー保持にする方法でも良いが
    // 現状ではポインタ保持なのでFlushの絡むseek処理のみこのように対応している。
    // (実際の所スレッド絡みなので毎回コピー保持したほうが丸いので、将来的にはそうなるかも)
    mDummyFrame.CopyFrom(mDecodedFrame, false);
    UpdateDecodedFrame(&mDummyFrame);
    UpdateDecodedFrameNext(nullptr);
    if (mVideoDecoder != nullptr) {
      // Syncしないと、ExtractorがSeekして少し読んだあとに
      // 非同期に呼ばれたFlushが挟まっておかしくなる可能性がある
      mVideoDecoder->FlushSync();
    }
    if (mAudioDecoder != nullptr) {
      mAudioDecoder->FlushSync();
    }
    mExtractor->SeekTo(arg);
    mTimer.ClearStartTime();
    mSawInputEOS  = false;
    mSawOutputEOS = false;
    // シーク動作のためにプリロード相当の処理をする
    Decode(true);
    break;

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

    // 前フレームがダミーフレームでなければ開放
    if (prevFrame != &mDummyFrame) {
      mVideoDecoder->ReleaseDecodedBufferIndex(prevFrame->bufIndex);
    }

    // 新フレームがダミーフレームではなく、
    // Audioトラックがない場合は、メディアタイマーをビデオフレームのPTSで更新
    if (!IsAudioAvailable() && newFrame != &mDummyFrame) {
      int64_t mediaTimeUs = ns_to_us(mDecodedFrame->timeStampNs);
      mTimer.SetCurrentMediaTime(ns_to_us(mediaTimeUs));
      if (!mTimer.IsStarted()) {
        mTimer.SetStartTime(mediaTimeUs);
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
MoviePlayerCore::GetAudio(uint8_t *frames, uint64_t frameCount, uint64_t *framesRead)
{
  // TODO WIP
  if (IsAudioAvailable()) {
    // TODO ReadData 相当の処理
    // mAudioDecoder->

    // - 足りない場合の処理
    // - peekがないので面倒…

#if 0 // WIP
    // Getされたフレームの先頭のPTSを現在のフレームタイムとして記録する
    // (この瞬間に再生に送られたと見なす)
    int64_t mediaTimeUs = ns_to_us(buf->timeStampNs);
    mTimer.SetCurrentMediaTime(mediaTimeUs);
    if (!mTimer.IsStarted()) {
      mTimer.SetStartTime(mediaTimeUs);
    }
#endif
  }

  // 音声トラックがない、あるいは必要なデータが充足していない
  if (framesRead) {
    *framesRead = 0;
  }
  return false;
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
      mTimer.SetStartTime();
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
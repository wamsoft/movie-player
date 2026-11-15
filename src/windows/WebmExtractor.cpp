#define MYLOG_TAG "WebmExtractor"
#include "BasicLog.h"
#include "WebmExtractor.h"
#include "Decoder.h"

#include <vector>
#include <algorithm>
#include <stdarg.h>

WebmExtractor::WebmExtractor()
: mCtx(nullptr)
, mTracks(0)
, mVideoTrack(-1)
, mAudioTrack(-1)
, mPkt(nullptr)
, mReader(nullptr)
{
  mIsReachedEOS     = false;
  mIsFirstRead      = true;
  mTimeStampNs      = -1;
  mDurationUs       = -1;
  mFrames           = 0;
  mFrameIndex       = 0;
  mCurrentTrack     = -1;
  mCurrentTrackType = TRACK_TYPE_UNKNOWN;
  mDiscardPadding   = 0;
  mIsKeyFrame       = false;
  mVideoAlphaMode   = false;
}

WebmExtractor::~WebmExtractor()
{
  if (mPkt) {
    nestegg_free_packet(mPkt);
    mPkt = nullptr;
  }
  if (mCtx) {
    nestegg_destroy(mCtx);
    mCtx = nullptr;
  }
  if (mReader) {
    delete mReader;
    mReader = nullptr;
  }
}

void
WebmExtractor::NestEggLogCallback(nestegg *ctx, unsigned int severity, char const *fmt,
                                  ...)
{
#if defined(DEBUG_INFO_NESTEGG)
#ifndef VERBOSE
  if (severity == NESTEGG_LOG_DEBUG) return;
#endif
  va_list ap;
  va_start(ap, fmt);
  char buf[8192];
  int size = vsprintf(buf, fmt, ap);
  if (severity >= NESTEGG_LOG_ERROR) { 
    LOGE("NestEgg %p: %s\n", ctx, buf);
  } else {
    LOGV("NestEgg %p: %s\n", ctx, buf);
  }
  va_end(ap);
#endif
}

int
WebmExtractor::MyRead(void *buffer, size_t length, void *userdata)
{
  return ((WebmExtractor *)userdata)->mReader->Read(buffer, length);
}

int
WebmExtractor::MySeek(int64_t offset, int whence, void *userdata)
{
  return ((WebmExtractor *)userdata)->mReader->Seek(offset, whence);
}

int64_t
WebmExtractor::MyTell(void *userdata)
{
  return ((WebmExtractor *)userdata)->mReader->Tell();
}

bool
WebmExtractor::Open(const std::string &filePath)
{
  if (filePath.empty()) {
    LOGE("invalid file name.\n");
    return false;
  }

  if (!(mReader = IMkvFileReader::Create(filePath.c_str()))) {
    LOGV("fail to open movie file: %s\n", filePath.c_str());
    return false;
  }

  return OpenSetup();
}


bool
WebmExtractor::Open(IMovieReadStream *stream)
{
  if (!stream) {
    LOGE("invalid stream.\n");
    return false;
  }

  if (!(mReader = IMkvFileReader::Create(stream))) {
    LOGV("fail to open movie stream\n");
    return false;
  }

  return OpenSetup();
}

bool
WebmExtractor::OpenSetup()
{
  int ret;

  const int64_t startPos = mReader->Tell();

  nestegg_io io = { &MyRead, &MySeek, &MyTell, this };
  ret           = nestegg_init(&mCtx, io, &NestEggLogCallback, -1);
  if (ret < 0) {
    LOGE("init error\n");
    return false;
  }

  uint64_t duration;
  ret = nestegg_duration(mCtx, &duration);
  if (ret < 0) {
    LOGE("unknown duration: using 10s default\n");
    duration = (uint64_t)1e9 * 10;
  }
  mDurationUs = (uint64_t)(duration / 1000);

  ret = nestegg_track_count(mCtx, &mTracks);
  if (ret < 0) {
    LOGE("unknown tracks\n");
    nestegg_destroy(mCtx);
    mCtx = nullptr;
    return false;
  }
  LOGV("tracks=%u\n", mTracks);

  EstimateFrameRates(startPos);

  return true;
}

void
WebmExtractor::EstimateFrameRates(int64_t startPos)
{
  // DefaultDuration の無い映像トラックを集める
  std::map<unsigned int, std::vector<uint64_t>> stamps;
  for (unsigned int t = 0; t < mTracks; ++t) {
    if (nestegg_track_type(mCtx, t) != NESTEGG_TRACK_VIDEO) continue;
    uint64_t dd = 0;
    if (nestegg_track_default_duration(mCtx, t, &dd) == 0 && dd > 0) continue;
    stamps[t];
  }
  if (stamps.empty()) {
    return;
  }

  // mCtx の読み位置を崩さないよう、別の nestegg で先頭からパケットを読み、
  // 終わったらリーダーの位置を戻す。mCtx の解析状態は mCtx 側に持たれていて
  // リーダーの位置さえ戻せば続きから読める。
  const int64_t curPos = mReader->Tell();
  if (curPos < 0 || mReader->Seek(startPos, NESTEGG_SEEK_SET) != 0) {
    return;
  }

  nestegg *ctx  = nullptr;
  nestegg_io io = { &MyRead, &MySeek, &MyTell, this };
  if (nestegg_init(&ctx, io, &NestEggLogCallback, -1) == 0) {
    // 1 トラックあたり最大 kMaxFrames 個、全体で kMaxPackets 個まで見る
    const size_t kMaxFrames  = 61;
    const int kMaxPackets    = 2000;
    size_t pending           = stamps.size();
    for (int n = 0; n < kMaxPackets && pending > 0; ++n) {
      nestegg_packet *pkt = nullptr;
      if (nestegg_read_packet(ctx, &pkt) <= 0) break;
      unsigned int track;
      uint64_t ts;
      if (nestegg_packet_track(pkt, &track) == 0 &&
          nestegg_packet_tstamp(pkt, &ts) == 0) {
        auto it = stamps.find(track);
        if (it != stamps.end() && it->second.size() < kMaxFrames) {
          it->second.push_back(ts);
          if (it->second.size() == kMaxFrames) --pending;
        }
      }
      nestegg_free_packet(pkt);
    }
    nestegg_destroy(ctx);
  }

  mReader->Seek(curPos, NESTEGG_SEEK_SET);

  // タイムスタンプは TimecodeScale (通常 1ms) で丸められているので、隣どうしの
  // 1 区間ではなく (最後 - 最初) / (個数 - 1) の平均間隔から求める。
  // 並びが表示順でない素材もありうるので並べ替えてから使う。
  for (auto &kv : stamps) {
    std::vector<uint64_t> &v = kv.second;
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    if (v.size() < 2) continue;
    const double spanNs = (double)(v.back() - v.front());
    if (spanNs <= 0) continue;
    const double fps = (double)(v.size() - 1) * 1e9 / spanNs;
    mEstimatedFrameRates[(int32_t)kv.first] = (float)fps;
    LOGV("track %u: no DefaultDuration, estimated fps=%f from %u frames\n",
         kv.first, fps, (unsigned)v.size());
  }
}

size_t
WebmExtractor::GetTrackCount()
{
  if (!mCtx) {
    LOGE("data source is not opened.\n");
    return 0;
  }
  return mTracks;
}

bool
WebmExtractor::GetTrackInfo(int32_t trackIndex, TrackInfo *info)
{
  assert(info != nullptr);

  if (!mCtx) {
    LOGE("data source is not opened.\n");
    return false;
  }

  if (trackIndex >= mTracks) {
    LOGE("invalid track index: trackIndex=%d\n", trackIndex);
    return false;
  }

  // トラックインデックス、トラック番号
  info->index   = trackIndex;
  info->codecId = (CodecId)nestegg_track_codec_id(mCtx, trackIndex);

  int type = nestegg_track_type(mCtx, trackIndex);
  if (type == NESTEGG_TRACK_VIDEO) {

    nestegg_video_params vparams;
    int ret = nestegg_track_video_params(mCtx, trackIndex, &vparams);
    if (ret < 0) {
      LOGE("unknown video track param\n");
      return false;
    }

    // DefaultDuration が無い素材では nestegg_track_default_duration は -1 を返し
    // 値を書かない。その場合は OpenSetup で先頭のパケットから求めた値を使い、
    // それも無ければ 0 (不明) とする。
    float frameRate = 0.0f;
    uint64_t default_duration = 0;
    if (nestegg_track_default_duration(mCtx, trackIndex, &default_duration) == 0 &&
        default_duration > 0) {
      frameRate = (float)(1.0 / ns_to_s((int64_t)default_duration));
    } else {
      auto it = mEstimatedFrameRates.find(trackIndex);
      if (it != mEstimatedFrameRates.end()) {
        frameRate = it->second;
      }
    }

    info->type         = TRACK_TYPE_VIDEO;
    info->v.width      = vparams.width;
    info->v.height     = vparams.height;
    info->v.frameRate  = frameRate;
    info->v.colorRange = (ColorRange)vparams.range;
    info->v.alphaMode  = vparams.alpha_mode;

  } else if (type == NESTEGG_TRACK_AUDIO) {

    nestegg_audio_params aparams;
    int ret = nestegg_track_audio_params(mCtx, trackIndex, &aparams);
    if (ret < 0) {
      LOGE("unknown video audio param\n");
      return false;
    }
    info->type         = TRACK_TYPE_AUDIO;
    info->a.channels   = aparams.channels;
    info->a.bitDepth   = aparams.depth;
    info->a.sampleRate = aparams.rate;
    info->a.codecDelay = aparams.codec_delay;
  }

  return true;
}

bool
WebmExtractor::GetCodecPrivateData(int32_t trackIndex,
                                   std::vector<std::vector<uint8_t>> &privateData)
{
  unsigned int dataCount;
  int ret = nestegg_track_codec_data_count(mCtx, trackIndex, &dataCount);
  if (ret < 0) {
    LOGE("failed to get codec private data count: err=%d\n", ret);
    return false;
  }
  privateData.resize(dataCount);

  for (int h = 0; h < dataCount; ++h) {
    uint8_t *data;
    size_t sizeBytes;
    ret = nestegg_track_codec_data(mCtx, trackIndex, h, &data, &sizeBytes);
    if (ret < 0) {
      LOGE("failed to get codec private data: err=%d\n", ret);
      return false;
    }

    privateData[h].resize(sizeBytes);
    memcpy(privateData[h].data(), data, sizeBytes);
  }

  return true;
}

bool
WebmExtractor::SeekTo(long long positionUs)
{
  if (!mCtx) {
    LOGE("data source is not opened.\n");
    return false;
  }

  int64_t posNs = us_to_ns((int64_t)positionUs);
  if (!nestegg_has_cues(mCtx)) {
    posNs = 0;
  }

  if (mVideoTrack >= 0) {
    nestegg_track_seek(mCtx, mVideoTrack, posNs);
  }
  if (mAudioTrack >= 0) {
    nestegg_track_seek(mCtx, mAudioTrack, posNs);
  }
  // カーソル情報をリセット
  if (mPkt) {
    nestegg_free_packet(mPkt);
    mPkt = nullptr;
  }

  mIsReachedEOS     = false;
  mIsFirstRead      = true;
  mTimeStampNs      = -1;
  mCurrentTrack     = -1;
  mCurrentTrackType = TRACK_TYPE_UNKNOWN;
  mFrames           = 0;
  mFrameIndex       = 0;

  return true;
}

bool
WebmExtractor::SelectTrack(TrackType type, int32_t trackIndex)
{
  switch (type) {
  case TRACK_TYPE_VIDEO: {
    nestegg_video_params vparams;
    int ret = nestegg_track_video_params(mCtx, trackIndex, &vparams);
    if (ret < 0) {
      LOGE("unknown video track param\n");
      return false;
    }
    mVideoTrack     = trackIndex;
    mVideoAlphaMode = vparams.alpha_mode;
  } break;
  case TRACK_TYPE_AUDIO:
    mAudioTrack = trackIndex;
    break;
  }
  return true;
}

void
WebmExtractor::CheckFirstTouch()
{
  if (mIsFirstRead) {
    Advance();
    mIsFirstRead = false;
  }
}

TrackType
WebmExtractor::NextFramePacketType()
{
  CheckFirstTouch();
  return mCurrentTrackType;
}

bool
WebmExtractor::ReadSampleData(FramePacket *packet)
{
  ASSERT(packet != nullptr, "invalid packet addr\n");

  CheckFirstTouch();

  if (mIsReachedEOS) {
    packet->InitAsEOS();
    return false;
  }

  if (!mPkt) {
    LOGE("invalid packet.\n");
    packet->InitAsEOS();
    return false;
  }

  unsigned char *data;
  size_t length;
  int ret = nestegg_packet_data(mPkt, mFrameIndex, &data, &length);
  if (ret < 0) {
    LOGV("end of packet frame\n");
    mIsReachedEOS = true;
    return false;
  }

  packet->Resize(length);
  if (packet->data == nullptr) {
    LOGE("packet data allocation failed.\n");
    return false;
  }

  memcpy(packet->data, data, length);
  packet->dataSize    = length;
  packet->trackNum    = mCurrentTrack;
  packet->isKeyFrame  = mIsKeyFrame;
  packet->arg         = mDiscardPadding;
  packet->type        = mCurrentTrackType;
  packet->timeStampNs = mTimeStampNs;

  if (mCurrentTrackType == TRACK_TYPE_VIDEO && mVideoAlphaMode) {
    unsigned char *add_data;
    size_t add_length;
    int ret = nestegg_packet_additional_data(mPkt, 1, &add_data, &add_length);
    if (ret != 0) {
      LOGE("packet additionaldata failed.\n");
      packet->ReleaseAdd();
    } else {
      packet->ResizeAdd(add_length);
      if (packet->adddata) {
        packet->adddataSize = add_length;
        memcpy(packet->adddata, add_data, add_length);
      }
    }
  }

#if defined(DEBUG_INFO_PACKET)
  packet->PrintInfo(mFrameIndex);
#endif

  return true;
}

bool
WebmExtractor::Advance()
{
  int ret;

  if (mPkt && ++mFrameIndex < mFrames) {
    return true;
  }

  if (mIsReachedEOS) {
    return true;
  }

  while (true) {

    if (mPkt) {
      nestegg_free_packet(mPkt);
      mPkt = nullptr;
    }

    mFrames           = 0;
    mFrameIndex       = 0;
    mCurrentTrackType = TRACK_TYPE_UNKNOWN;
    mDiscardPadding   = 0;
    mIsKeyFrame       = false;

    ret = nestegg_read_packet(mCtx, &mPkt);
    if (ret == 0) {
#if defined(DEBUG_INFO_NESTEGG)
      LOGV("End of Stream\n");
#endif
      mIsReachedEOS = true;
      break;
    }

    if (ret < 0) {
      LOGE("  failed: read packet\n");
      return false;
    }

    mIsKeyFrame = (nestegg_packet_has_keyframe(mPkt) == NESTEGG_PACKET_HAS_KEYFRAME_TRUE);

    // padding(DiscardPadding) は、負数ならブロック先頭、正数ならブロック末尾の
    // 無音のデータ区間を示す。単位はナノ秒。再生側でドロップすること、となっている。
    // AUDIOトラックのみ有効な情報で、とりあえず考えないでおく。

    ret = nestegg_packet_discard_padding(mPkt, &mDiscardPadding);
    if (ret < 0) {
      // LOGE("  failed: packet: discard padding");
      // nestegg_free_packet(mPkt);
      // mPkt = nullptr;
      // return false;
    }
    ret = nestegg_packet_track(mPkt, &mCurrentTrack);
    if (ret < 0) {
      LOGE("  failed: packet: track\n");
      nestegg_free_packet(mPkt);
      mPkt = nullptr;
      return false;
    }
    ret = nestegg_packet_count(mPkt, &mFrames);
    if (ret < 0) {
      LOGE("  failed: packet: frame\n");
      nestegg_free_packet(mPkt);
      mPkt = nullptr;
      return false;
    }
    ret = nestegg_packet_tstamp(mPkt, &mTimeStampNs);
    if (ret < 0) {
      LOGE("  failed: packet: time stamp\n");
      nestegg_free_packet(mPkt);
      mPkt = nullptr;
      return false;
    }

    if (mCurrentTrack == mVideoTrack) {
      mCurrentTrackType = TRACK_TYPE_VIDEO;
      break;
    }

    if (mCurrentTrack == mAudioTrack) {
      mCurrentTrackType = TRACK_TYPE_AUDIO;
      break;
    }
  };

  return true;
}

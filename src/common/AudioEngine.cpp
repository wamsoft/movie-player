#define MYLOG_TAG "AudioEngine"
#include "BasicLog.h"
#include "AudioEngine.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// -----------------------------------------------------------------------------
// miniaudio logger
// -----------------------------------------------------------------------------
static void
my_ma_logger(void *pUserData, ma_uint32 level, const char *pMessage)
{
  if (level >= MA_LOG_LEVEL_WARNING) {
    LOGE("miniaudio: %s\n", pMessage);
  } else {
    LOGV("miniaudio: %s\n", pMessage);
  }
}

// -----------------------------------------------------------------------------
// miniaudio data source vtable
// -----------------------------------------------------------------------------
static ma_result
my_data_source_read(ma_data_source *pDataSource, void *pFramesOut, ma_uint64 frameCount,
                    ma_uint64 *pFramesRead)
{
  my_data_source *self = (my_data_source *)pDataSource;
  if (self) {
    AudioEngine *engine = self->engine;
    if (engine) {
      return engine->ReadData(pFramesOut, frameCount, pFramesRead);
    }
  }
  // Read data here. Output in the same format returned by
  // my_data_source_get_data_format().
  return MA_AT_END;
}

static ma_result
my_data_source_seek(ma_data_source *pDataSource, ma_uint64 frameIndex)
{
  return MA_NOT_IMPLEMENTED;
}

static ma_result
my_data_source_get_data_format(ma_data_source *pDataSource, ma_format *pFormat,
                               ma_uint32 *pChannels, ma_uint32 *pSampleRate,
                               ma_channel *pChannelMap, size_t channelMapCap)
{
  // Return the format of the data here.
  my_data_source *self = (my_data_source *)pDataSource;
  if (pFormat) {
    *pFormat = self->format;
  }
  if (pChannels) {
    *pChannels = self->channels;
  }
  if (pSampleRate) {
    *pSampleRate = self->sampleRate;
  }
  return MA_SUCCESS;
}

static ma_result
my_data_source_get_cursor(ma_data_source *pDataSource, ma_uint64 *pCursor)
{
  if (pCursor) {
    *pCursor = 0;
  }
  return MA_NOT_IMPLEMENTED;
}

static ma_result
my_data_source_get_length(ma_data_source *pDataSource, ma_uint64 *pLength)
{
  if (pLength) {
    *pLength = 0;
  }
  return MA_NOT_IMPLEMENTED;
};

static ma_data_source_vtable s_my_data_source_vtable = {
  my_data_source_read,
  my_data_source_seek,
  my_data_source_get_data_format,
  my_data_source_get_cursor,
  my_data_source_get_length,
  NULL,
  MA_DATA_SOURCE_SELF_MANAGED_RANGE_AND_LOOP_POINT
};

// -----------------------------------------------------------------------------
// AudioEngine
// -----------------------------------------------------------------------------
AudioEngine::AudioEngine()
: mDataPos(0)
, mFrameSize(0)
{}

AudioEngine::~AudioEngine() {}

bool
AudioEngine::Init(AudioFormat format, int32_t channels, int32_t sampleRate)
{
  ma_log_callback_init(my_ma_logger, NULL);

  // エンジン初期化
  ma_result result = ma_engine_init(NULL, &mEngine);
  if (result != MA_SUCCESS) {
    LOGE("failed to initialize miniaudio engine: err=%d\n", result);
    return false;
  }

  // ソース初期化
  ma_format mf = ma_format_unknown;
  switch (format) {
  case AUDIO_FORMAT_U8:
    mf         = ma_format_u8;
    mFrameSize = channels * 1;
    break;
  case AUDIO_FORMAT_S16:
    mf         = ma_format_s16;
    mFrameSize = channels * 2;
    break;
  case AUDIO_FORMAT_F32:
    mf         = ma_format_f32;
    mFrameSize = channels * 4;
    break;
  default:
    ASSERT(false, "Unknown audio format: format=%d\n", format);
    return false;
  }
  mSource.format     = mf;
  mSource.channels   = channels;
  mSource.sampleRate = sampleRate;
  mSource.engine     = this;

  auto dataSourceConfig   = ma_data_source_config_init();
  dataSourceConfig.vtable = &s_my_data_source_vtable;

  result = ma_data_source_init(&dataSourceConfig, &mSource);
  if (result != MA_SUCCESS) {
    LOGE("failed to initialize miniaudio data source: err=%d\n", result);
    return false;
  }

  // サウンド初期化
  result = ma_sound_init_from_data_source(&mEngine, &mSource, 0, NULL, &mSound);
  if (result != MA_SUCCESS) {
    LOGE("failed to initialize miniaudio sound engine: err=%d\n", result);
    return false;
  }

  // 初期ボリューム取得
  mVolume = ma_sound_get_volume(&mSound);
  LOGV("initial sound volume: %f\n", mVolume);

  // ステート初期化
  mDataPos = 0;

  LOGV("miniaudio engine initialized!\n");

  return true;
}

void
AudioEngine::Done()
{
  Stop();
  ma_engine_uninit(&mEngine);
}

void
AudioEngine::Start()
{
  ma_result result = ma_sound_start(&mSound);
  if (result != MA_SUCCESS) {
    LOGE("failed to start sound: err=%d\n", result);
  }
}

void
AudioEngine::Stop()
{
  ma_result result = ma_sound_stop(&mSound);
  if (result != MA_SUCCESS) {
    LOGE("failed to stop sound: err=%d\n", result);
  }
}

void
AudioEngine::SetVolume(float vol)
{
  if (mVolume != vol) {
    if (vol < 0.0f) {
      vol = 0.0f;
    }
    if (vol > 1.0f) {
      vol = 1.0f;
    }
    mVolume = vol;

    ma_sound_set_volume(&mSound, vol);
  }
}

float
AudioEngine::Volume() const
{
  return mVolume;
}

void
AudioEngine::Enqueue(void *data, size_t size, bool last)
{
  std::lock_guard<std::mutex> lock(mDataMutex);

  mDataQueue.push(DataBuffer(data, size, last));
  // LOGV("enqueue audio buffer: size=%llu, count=%llu\n", size, mDataQueue.size());
}

void
AudioEngine::ClearQueue()
{
  std::lock_guard<std::mutex> lock(mDataMutex);

  mDataQueue = {};
}

ma_result
AudioEngine::ReadData(void *pFramesOut, ma_uint64 frameCount, ma_uint64 *pFramesRead)
{
  std::lock_guard<std::mutex> lock(mDataMutex);

  uint8_t *dst    = (uint8_t *)pFramesOut;
  ma_uint64 size  = frameCount * mFrameSize;
  ma_uint64 count = 0;
  bool last       = false;

  while (!last && size > 0 && mDataQueue.size() > 0) {
    const DataBuffer &data = mDataQueue.front();
    int remain             = data.size - mDataPos;
    if (size < remain) {
      memcpy(dst, (uint8_t *)data.data + mDataPos, size);
      dst += size;
      count += (size / mFrameSize);
      mDataPos += size;
      size = 0;
    } else {
      memcpy(dst, (uint8_t *)data.data + mDataPos, remain);
      if (data.last) {
        last = true;
      }
      mDataQueue.pop();
      dst += remain;
      count += (remain / mFrameSize);
      mDataPos = 0;
      size -= remain;
    }
  }

  ma_result result = last ? MA_AT_END : MA_SUCCESS;

  // デコードが間に合っていない場合の処理
  if (!last && count < frameCount) {
#if 0
    // デコードが間に合っていない場合はBUSYを返す
    result = MA_BUSY;
#else
    // デコードが間に合っていない場合は無音を埋めて返す
    size_t gapBytes = (frameCount - count) * mFrameSize;
    memset(dst, 0, gapBytes);
    count = frameCount;
#endif
  }

  if (pFramesRead) {
    *pFramesRead = count;
  }

  return result;
}
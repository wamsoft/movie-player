#pragma once

#include <mutex>
#include <queue>

#include "CommonUtils.h"
#include "Constants.h"

#include "miniaudio.h"

class AudioEngine;
struct my_data_source
{
  ma_data_source_base base;
  ma_format format;
  ma_uint32 channels;
  ma_uint32 sampleRate;
  AudioEngine *engine;
};

class AudioEngine
{
public:
  AudioEngine();
  ~AudioEngine();

  bool Init(AudioFormat format, int32_t channels, int32_t sampleRate);
  void Done();

  void Start();
  void Stop();

  void SetVolume(float vol);
  float Volume() const;

  void Enqueue(void *data, size_t size, bool last);
  void ClearQueue();

  ma_result ReadData(void *pFramesOut, ma_uint64 frameCount, ma_uint64 *pFramesRead);

private:
  float mVolume;
  ma_sound mSound;
  ma_engine mEngine;
  my_data_source mSource;

  struct DataBuffer
  {
    void *data;
    size_t size;
    bool last;
    DataBuffer(void *data, size_t size, bool last)
    : data(data)
    , size(size)
    , last(last)
    {}
  };

  std::queue<DataBuffer> mDataQueue;
  std::mutex mDataMutex;
  off_t mDataPos;
  int32_t mFrameSize;
};

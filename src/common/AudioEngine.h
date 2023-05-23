#pragma once

#include <mutex>
#include <queue>

#include "CommonUtils.h"
#include "Constants.h"

#include "miniaudio.h"

class MoviePlayerCore;

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

  bool Init(MoviePlayerCore *player, AudioFormat format, int32_t channels,
            int32_t sampleRate);
  void Done();

  void Start();
  void Stop();

  void SetVolume(float vol);
  float Volume() const;

  ma_result ReadData(void *pFramesOut, ma_uint64 frameCount, ma_uint64 *pFramesRead);

private:
  float mVolume;
  ma_sound mSound;
  ma_engine mEngine;
  my_data_source mSource;
  int32_t mFrameSize;

  MoviePlayerCore *mPlayer;
};

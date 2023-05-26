#pragma once

#include <IMoviePlayer.h>

#include <cstdint>

// ムービープレイヤー実装クラス
class MoviePlayer : public IMoviePlayer
{
public:
  MoviePlayer();
  virtual ~MoviePlayer();

  bool Open(const char *filepath);

  virtual void Play(bool loop = false) override;
  virtual void Stop() override;
  virtual void Pause() override;
  virtual void Resume() override;
  virtual void Seek(int64_t posUs) override;
  virtual void SetLoop(bool loop) override;
  virtual void SetColorFormat(ColorFormat format) override;

  // video info
  virtual bool IsVideoAvailable() const override;
  virtual int32_t Width() const override;
  virtual int32_t Height() const override;
  virtual float FrameRate() const override;

  // audio info
  virtual bool IsAudioAvailable() const override;
  virtual void GetAudioFormat(AudioFormat *format) const override;

  // info
  virtual int64_t Duration() const override;
  virtual int64_t Position() const override;
  virtual bool IsPlaying() const override;
  virtual bool Loop() const override;

  // TODO DELETE?
  virtual void RenderFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                           ColorFormat format = COLOR_UNKNOWN) override;

  virtual bool GetVideoFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                             uint64_t *timeStampUs) override;
  virtual bool GetAudioFrame(uint8_t *frames, int64_t frameCount, uint64_t *framesRead,
                             uint64_t *timeStampUs) override;

private:
  void Init();
  void Done();

private:
  class MoviePlayerCore *mPlayer;
};
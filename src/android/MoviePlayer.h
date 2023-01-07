#pragma once

#include <IMoviePlayer.h>

#include <cstdint>

#include <android/asset_manager.h>

// ムービープレイヤー実装クラス
class MoviePlayer : public IMoviePlayer
{
public:
  MoviePlayer();
  virtual ~MoviePlayer();

  bool Open(const char *filepath);
  bool Open(int fd, off_t offset, off_t length);
  bool Open(AAssetManager *mgr, const char *filepath);

  virtual void Play(bool loop = false) override;
  virtual void Stop() override;
  virtual void Pause() override;
  virtual void Resume() override;
  virtual void Seek(int64_t posUs) override;
  virtual void SetLoop(bool loop) override;

  virtual void SetColorFormat(ColorFormat format) override;

  virtual int32_t Width() const override;
  virtual int32_t Height() const override;

  virtual bool IsAudioAvailable() const override;
  virtual void GetAudioFormat(AudioFormat *format) const override;

  virtual int64_t Duration() const override;
  virtual int64_t Position() const override;
  virtual bool IsPlaying() const override;
  virtual bool Loop() const override;

  virtual void RenderFrame(uint8_t *dst, int32_t w, int32_t h, int32_t strideBytes,
                           ColorFormat format = UNKNOWN) override;

  virtual void SetOnAudioDecoded(OnAudioDecoded func, void *userPtr) override;

private:
  void Init();
  void Done();

private:
  class MoviePlayerCore *mPlayer;
  AAsset *mAsset;
};
#pragma once

#include <IMoviePlayer.h>

#include <cstdint>

// ムービープレイヤー実装クラス
class MoviePlayer : public IMoviePlayer
{
public:
  MoviePlayer(InitParam &param);
  virtual ~MoviePlayer();

  bool Open(const char *filepath);
  bool Open(IMovieReadStream *stream);

  virtual State GetState() const override;

  virtual void Play(bool loop = false) override;
  virtual void Stop() override;
  virtual void Pause() override;
  virtual void Resume() override;
  virtual void Seek(int64_t posUs) override;
  virtual void SetLoop(bool loop) override;

  // video info
  virtual bool IsVideoAvailable() const override;
  virtual void GetVideoFormat(VideoFormat *format) const override;

  // audio info
  virtual bool IsAudioAvailable() const override;
  virtual void GetAudioFormat(AudioFormat *format) const override;

  virtual void SetVolume(float volume) override;
  virtual float Volume() const override;

  // info
  virtual int64_t Duration() const override;
  virtual int64_t Position() const override;
  virtual bool IsPlaying() const override;
  virtual bool Loop() const override;

  virtual void SetOnState(OnState func, void *userPtr);

  virtual void SetOnVideoDecoded(OnVideoDecoded callback);

private:
  void Init();
  void Done();

private:
  class MoviePlayerCore *mPlayer;
  InitParam mInitParam;
};
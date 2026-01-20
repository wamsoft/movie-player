#include <string>

#include <jni.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "media/NdkMediaDataSource.h"

// #define LOG_NDEBUG 0
#define TAG       "MoviePlayer"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOG_ASSERT(_cond, ...) \
  if (!(_cond))                \
  __android_log_assert("conditional", TAG, __VA_ARGS__)

// -----------------------------------------------------------------------------
// ムービー再生テスト用コード
// -----------------------------------------------------------------------------
#include "IMoviePlayer.h"

#ifdef __cplusplus
extern "C"
{
#endif

  //
  static AAssetManager *s_assetManager = nullptr;

  // staticでテスト用インスタンスへのポインタを用意しておく
  static IMoviePlayer *s_player = nullptr;
  static int s_videoWidth = 0;
  static int s_videoHeight = 0;
  static char *s_videoBuffer = nullptr;
  static int s_videoBufferSize = 0;
  static bool s_updateFlag = false;
  
  class AssetReadStream : public IMovieReadStream
  {
    AAsset *mAsset;
    int mRefCount;

  public:
    AssetReadStream(AAsset *asset) : mAsset(asset), mRefCount(1) {}
    virtual ~AssetReadStream()
    {
      if (mAsset) {
        AAsset_close(mAsset);
      }
    }
    virtual int AddRef(void)
    {
      return ++mRefCount;
    }
    virtual int Release(void)
    {
      int count = --mRefCount;
      if (count <= 0) {
        delete this;
      }
      return count;
    }
    virtual size_t Read(void *buf, size_t size)
    {
      int r = AAsset_read(mAsset, buf, size);
      return (r < 0) ? 0 : r;
    }
    virtual int64_t Tell() const
    {
      off_t cur = AAsset_seek(mAsset, 0, SEEK_CUR);
      return (int64_t)cur;
    }
    virtual void Seek(int64_t offset, int origin)
    {
      AAsset_seek(mAsset, (off_t)offset, origin);
    }
    virtual size_t Size() const
    {
      off64_t len = AAsset_getLength64(mAsset);
      return (size_t)len;
    }
  };

  // AssetManagerを登録
  JNIEXPORT void JNICALL Java_jp_wamsoft_testmovieplayer_TestMovieView_setAssetManager(
    JNIEnv *env, jclass clazz, jobject assetManager)
  {
    s_assetManager = AAssetManager_fromJava(env, assetManager);
  }

  // ファイル名を与えてプレイヤーをクリエイト
  JNIEXPORT jboolean JNICALL Java_jp_wamsoft_testmovieplayer_TestMovieView_createPlayer(
    JNIEnv *env, jclass clazz, jstring jpath)
  {
    jboolean success = false;

    // 指定されたファイルをオープン
    const char *tmp_path_str = env->GetStringUTFChars(jpath, nullptr);
    LOGV("open file: %s", tmp_path_str);

    if (s_player) {
      delete s_player;
      s_player = nullptr;
    }

    if (s_assetManager) {
      AAsset *asset = AAssetManager_open(s_assetManager, tmp_path_str, AASSET_MODE_RANDOM);
      if (asset) {
        IMovieReadStream *stream = new AssetReadStream(asset);
        IMoviePlayer::InitParam param;
        param.Init();
        param.videoColorFormat = IMoviePlayer::COLOR_RGBA;
        s_player = IMoviePlayer::CreateMoviePlayer(stream, param);
        if (s_player) {
          s_player->SetOnVideoDecoded(
            [](int w, int h, IMoviePlayer::DestUpdater updater) {
              if (w != s_videoWidth || h != s_videoHeight) {
                s_videoWidth = w;
                s_videoHeight = h;
                s_videoBufferSize = w * h * 4;
                if (s_videoBuffer) {
                  delete[] s_videoBuffer;
                  s_videoBuffer = nullptr;
                }
                s_videoBuffer = new char[s_videoBufferSize];
              }
              updater(s_videoBuffer, w * 4);
              s_updateFlag = true;
            });
        }
        stream->Release();
      }
    }

    success = (s_player != nullptr);

    env->ReleaseStringUTFChars(jpath, tmp_path_str);

    return success;
  }

  // 更新バッファ
  JNIEXPORT jobject JNICALL Java_jp_wamsoft_testmovieplayer_TestMovieView_getUpdatedBuffer(JNIEnv* env, jclass clazz) {
    if (!s_updateFlag) { 
      return NULL; // 更新なし 
    } 
    s_updateFlag = false; // フラグをクリア
    return env->NewDirectByteBuffer(s_videoBuffer, s_videoBufferSize); 
  }

  // width
  JNIEXPORT jint JNICALL
  Java_jp_wamsoft_testmovieplayer_TestMovieView_width(JNIEnv *env, jclass /* this */)
  {
    if (s_player) {
      IMoviePlayer::VideoFormat format;
      s_player->GetVideoFormat(&format);
      return format.width;
    }
    return -1;
  }

  // height
  JNIEXPORT jint JNICALL
  Java_jp_wamsoft_testmovieplayer_TestMovieView_height(JNIEnv *env, jclass /* this */)
  {
    if (s_player) {
      IMoviePlayer::VideoFormat format;
      s_player->GetVideoFormat(&format);
      return format.height;
    }
    return -1;
  }

  // 再生状態
  JNIEXPORT jboolean JNICALL
  Java_jp_wamsoft_testmovieplayer_TestMovieView_isPlaying(JNIEnv *env, jclass /* this */)
  {
    if (s_player) {
      return s_player->IsPlaying();
    }
    return false;
  }

  // 再生開始
  JNIEXPORT void JNICALL Java_jp_wamsoft_testmovieplayer_TestMovieView_startPlayer(
    JNIEnv *env, jclass clazz, jboolean loop)
  {
    if (s_player) {
      s_player->Play(loop);
    }
  }

  // 再生停止
  JNIEXPORT void JNICALL
  Java_jp_wamsoft_testmovieplayer_TestMovieView_stopPlayer(JNIEnv *env, jclass clazz)
  {
    if (s_player) {
      s_player->Stop();
    }
  }

  // 再生停止
  JNIEXPORT void JNICALL
  Java_jp_wamsoft_testmovieplayer_TestMovieView_shutdownPlayer(JNIEnv *env, jclass clazz)
  {
    if (!s_player) {
      return;
    }
    delete s_player;
    s_player = nullptr;
  }

  // 以下はUI側の実装の関係で、TestMovieViewではなくMainActivity用
  // duration
  JNIEXPORT jint JNICALL
  Java_jp_wamsoft_testmovieplayer_MainActivity_duration(JNIEnv *env, jobject /* this */)
  {
    if (s_player) {
      return s_player->Duration();
    }
    return -100;
  }

  // position
  JNIEXPORT jint JNICALL
  Java_jp_wamsoft_testmovieplayer_MainActivity_position(JNIEnv *env, jobject /* this */)
  {
    if (s_player) {
      return s_player->Position();
    }
    return -1;
  }

#ifdef __cplusplus
}
#endif

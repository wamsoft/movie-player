#include <cstdio>
#include <string>
#include <thread>
#include <chrono>
#include <random>

#include "GLRenderer.h"
#include "IMoviePlayer.h"

#define TEST_PRE_CONV_YUV // 固定カラーフォーマット指定を使用
// #define TEST_DIB_MODE  // DIBビットマップ想定の処理テストを行う(表示は逆さまになる)

static int64_t seekStepUs = 2'000'000; // 2s
static bool isPause       = false;
void
key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
  auto player = (IMoviePlayer *)glfwGetWindowUserPointer(window);
  if (!player->IsPlaying()) {
    return;
  }
  int64_t pos = player->Position();
  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_LEFT) {
      player->Seek(pos - seekStepUs);
    } else if (key == GLFW_KEY_RIGHT) {
      player->Seek(pos + seekStepUs);
    } else if (key == GLFW_KEY_SPACE) {
      if (isPause) {
        player->Resume();
      } else {
        player->Pause();
      }
      isPause = !isPause;
    } else if (key == GLFW_KEY_ENTER) {
      player->Seek(0);
    } else if (key == GLFW_KEY_ESCAPE) {
      player->Stop();
    }
  }
}

int
main(int argc, char *argv[])
{
  if (argc == 1) {
    printf("  Usage: %s <input file> \n", argv[0]);
    return -1;
  }
  std::string testFilePath = argv[1];

  // 乱数
  std::random_device rnd;
  std::mt19937 mt(rnd());

  // テスト用描画環境をセットアップ
  GLFWwindow *glfwWin = nullptr;
  Renderer renderer;
  glfwWin = renderer.InitGL(1920, 1080, "Movie Player Test App");
  renderer.InitRenderer();
  renderer.EnableVSync(true);
  renderer.SetTextureRenderScale(0.9f);

  // キーコールバック設定
  glfwSetKeyCallback(glfwWin, key_callback);

  // MoviePlayer を作成
#if defined(TEST_PRE_CONV_YUV) && !defined(TEST_DIB_MODE)
  // 固定出力フォーマット指定
  // これをしないと毎フレーム MoviePlayer::RenderFrame() の度に
  // YUV->RGB変換が行われるので効率が悪くなります。
  IMoviePlayer::ColorFormat format = IMoviePlayer::COLOR_BGRA;
#else
  // UNKNOWN の場合はなプレイヤー内部ではYUV変換を行わない
  IMoviePlayer::ColorFormat format = IMoviePlayer::COLOR_NOCONV;
#endif
  IMoviePlayer *player = IMoviePlayer::CreateMoviePlayer(testFilePath.c_str(), format);
  if (player == nullptr) {
    printf("Failed to create MoviePlayer! \n");
    goto finish;
  }
  // GLFWウィンドウにプレイヤーを紐付ける(イベントハンドラ用)
  glfwSetWindowUserPointer(glfwWin, player);

  {
    // テストメモ：
    // 寸法情報などは、Open()後はPlay()前に取得可能であることを想定しているので
    // テストではそのような順序になるようにしてある。将来的にもしそこでエラーに
    // なる場合はしかるべく対処すること。

    // 情報取得
    int32_t w      = player->Width();
    int32_t h      = player->Height();
    uint64_t total = player->Duration();
    printf("MOVIE: Width = %d Height = %d\n", w, h);

    // pause/resume テスト用
    std::uniform_int_distribution<> rand(0, total / 4);
    int64_t pausePoint  = rand(mt);
    int32_t pauseFrames = 120;

    // RenderFrame() で描画済みフレームを受け取るバッファを用意
    int pixelBytes  = w * h * 4;
    uint8_t *pixels = new uint8_t[pixelBytes];
    memset(pixels, 0xff, pixelBytes);

    // 描画済みフレームのバッファを表示するためのテクスチャを準備
    // テストピクセルデータで埋めておく
    uint32_t *p = (uint32_t *)pixels;
    renderer.InitTexture(w, h);
    for (size_t y = 0; y < h; y++) {
      for (size_t x = 0; x < w; x++) {
        *p = 0xFF00FF00;
        p++;
      }
    }
    renderer.UpdateTexture(pixels, pixelBytes);

    // 再生開始
    bool pausing = false;
    bool loop    = true;
    player->Play(loop);

    // 疑似レンダリングループ
    // 再生終了か画面タッチで終了
    int frameCount = 0;
    while (!glfwWindowShouldClose(glfwWin)) {
      if (!player->IsPlaying()) {
        printf("MoviePlayerTest: play finished\n");
        break;
      }

#if defined(TEST_DIB_MODE)
      // DIBの逆stride状態を想定したケース用
      player->RenderFrame(pixels + w * (h - 1) * 4, w, h, -1 * w * 4, IMoviePlayer::COLOR_BGRA);
#else
      player->RenderFrame(pixels, w, h, w * 4, IMoviePlayer::COLOR_BGRA);
#endif
      renderer.UpdateTexture(pixels, pixelBytes);
      renderer.Render(frameCount);

      glfwSwapBuffers(glfwWin);
      glfwPollEvents();
      frameCount++;
      // printf(" Time = %lld / %lld usec \n", player->Position(), total);
    }

    delete[] pixels;
  }

finish:
  if (player != nullptr) {
    delete player;
    player = nullptr;
  }

  renderer.DoneGL();

  return 0;
}
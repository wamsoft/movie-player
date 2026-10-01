#pragma once

#include <cstdint>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

struct Message
{
  int32_t what;
  int64_t arg;
  void *obj;
  bool quit;
};

class MessageLooper
{
public:
  MessageLooper();
  virtual ~MessageLooper();

  // MEMO PostMessageだとwindows.hを導入する環境でマクロに荒らされるので変名した
  void Post(int32_t what, int64_t arg = 0, void *data = nullptr, bool flush = false);
  void QuitLoop();

  // return: メッセージを処理した場合はtrue
  virtual void HandleMessage(int32_t what, int64_t arg, void *data);

protected:
  static const int32_t MSG_SPECIAL = INT32_MAX;

protected:
  void StartThread();
  void StopThread();
  void PostQuitMessage();
  void AddMessage(Message &&msg, bool flush);
  void MessageLoop();
  // 次のメッセージが届くか timeoutUs 経過するまで待つ (メッセージは取り出さない)。
  // HandleMessage の中から「今回は仕事が無かったので少し待ってから続ける」用。
  // return: メッセージが届いていれば true
  bool WaitForMessage(int64_t timeoutUs);
  bool IsRunning() const { return mIsRunning; }

protected:
  bool mIsRunning;
  std::thread mWorker;

#if defined(_WIN32)
  // WaitForMessage 用 (HANDLE を windows.h 無しで持つため void*)。
  // condition_variable::wait_for は Windows では既定のタイマ分解能 (~15.6ms) でしか
  // 起きないので、高分解能の待機タイマとメッセージ到着イベントを並べて待つ。
  void *mWakeEvent;
  void *mWaitTimer;
#endif

  // メッセージキュー管理
  std::queue<Message> mMessageQueue;
  std::mutex mMessageMutex;
  std::condition_variable mMessageCond;
};
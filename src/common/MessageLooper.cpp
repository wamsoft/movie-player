#define MYLOG_TAG "MessageLooper"
#include "BasicLog.h"

#include "CommonUtils.h"
#include "MessageLooper.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

MessageLooper::MessageLooper()
: mIsRunning(false)
{
#if defined(_WIN32)
  mWakeEvent = ::CreateEventW(NULL, FALSE, FALSE, NULL); // auto-reset
  // Win10 1803+ の高分解能タイマ (~0.5ms)。無い環境では通常のタイマ
  mWaitTimer = ::CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                        TIMER_ALL_ACCESS);
  if (mWaitTimer == NULL) {
    mWaitTimer = ::CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
  }
#endif
}

MessageLooper::~MessageLooper()
{
  if (mIsRunning) {
    LOGV("MessageLooper deleted while still running. "
         "Some messages will not be processed\n");
    QuitLoop();
  }
#if defined(_WIN32)
  if (mWakeEvent) ::CloseHandle((HANDLE)mWakeEvent);
  if (mWaitTimer) ::CloseHandle((HANDLE)mWaitTimer);
#endif
}

void
MessageLooper::StartThread()
{
  // looperスレッド開始
  mWorker    = std::thread([this] { MessageLoop(); });
  mIsRunning = true;
}

void
MessageLooper::StopThread()
{
  // looperスレッド停止
  if (mIsRunning) {
    QuitLoop();
  }
}

void
MessageLooper::QuitLoop()
{
  PostQuitMessage();
  mWorker.join();

  mIsRunning = false;
}

void
MessageLooper::PostQuitMessage()
{
  bool doQuit  = true;
  bool doFlush = true;
  AddMessage({ MSG_SPECIAL, 0, nullptr, doQuit }, doFlush);
}

void
MessageLooper::Post(int32_t what, int64_t arg, void *data, bool flush)
{
  AddMessage({ what, arg, data, false }, flush);
}

void
MessageLooper::AddMessage(Message &&msg, bool flush)
{
  std::lock_guard<std::mutex> lk(mMessageMutex);

  if (flush) {
    // std::queue には clear() がない
    mMessageQueue = {};
  }
  mMessageQueue.push(msg);
  mMessageCond.notify_all();
#if defined(_WIN32)
  if (mWakeEvent) ::SetEvent((HANDLE)mWakeEvent);
#endif
}

void
MessageLooper::MessageLoop()
{
  while (true) {
    Message msg;
    {
      // condition_variableにはunique_lockが必要(lock_guardではだめ)
      std::unique_lock<std::mutex> ulk(mMessageMutex);

      mMessageCond.wait(ulk, [this] { return !mMessageQueue.empty(); });
      msg = mMessageQueue.front();
      mMessageQueue.pop();
    }

    // 終了メッセージなのでループを抜ける
    if (msg.quit) {
      // LOGV("MessageLooper: Quit message arrived.\n");
      return;
    }

    HandleMessage(msg.what, msg.arg, msg.obj);
  }
}

bool
MessageLooper::WaitForMessage(int64_t timeoutUs)
{
#if defined(_WIN32)
  // 取り出し済みのメッセージ (自分で投げ直した MSG_DECODE 等) の通知が残っていると
  // 待たずに抜けてしまうので、キューを確かめる「前」に到着イベントを下ろす
  // (確かめた後に届いたものは改めてイベントが立つ)
  if (mWakeEvent) ::ResetEvent((HANDLE)mWakeEvent);
#endif
  {
    std::lock_guard<std::mutex> lk(mMessageMutex);
    if (!mMessageQueue.empty() || timeoutUs <= 0) {
      return !mMessageQueue.empty();
    }
  }
#if defined(_WIN32)
  if (mWakeEvent && mWaitTimer) {
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)timeoutUs * 10; // us -> 100ns 単位・相対
    if (::SetWaitableTimer((HANDLE)mWaitTimer, &due, 0, NULL, NULL, FALSE)) {
      HANDLE handles[2] = { (HANDLE)mWakeEvent, (HANDLE)mWaitTimer };
      ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
      ::CancelWaitableTimer((HANDLE)mWaitTimer);
      std::lock_guard<std::mutex> lk(mMessageMutex);
      return !mMessageQueue.empty();
    }
  }
#endif
  std::unique_lock<std::mutex> ulk(mMessageMutex);
  return mMessageCond.wait_for(ulk, std::chrono::microseconds(timeoutUs),
                               [this] { return !mMessageQueue.empty(); });
}

void
MessageLooper::HandleMessage(int32_t what, int64_t arg, void *obj)
{}

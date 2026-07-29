#ifndef PERIODIC_TASK_H
#define PERIODIC_TASK_H

#include <Arduino.h>

class PeriodicTask
{
public:
  using Callback = void (*)();

  PeriodicTask(Callback callback, uint32_t intervalMs)
      : callback_(callback), intervalMs_(intervalMs)
  {
  }

  void start(uint32_t now, bool runImmediately = false)
  {
    lastRunAt_ = runImmediately ? now - intervalMs_ : now;
    started_ = true;
  }

  void setInterval(uint32_t intervalMs)
  {
    intervalMs_ = intervalMs;
  }

  void run(uint32_t now)
  {
    if (!started_)
      start(now);

    if (intervalMs_ == 0)
    {
      lastRunAt_ = now;
      callback_();
      return;
    }

    if (now - lastRunAt_ < intervalMs_)
      return;

    // Keep tasks on a fixed timeline so callback execution time is not added
    // to every interval. Skip missed slots instead of running a backlog.
    lastRunAt_ += ((now - lastRunAt_) / intervalMs_) * intervalMs_;
    callback_();
  }

private:
  Callback callback_;
  uint32_t intervalMs_;
  uint32_t lastRunAt_ = 0;
  bool started_ = false;
};

#endif

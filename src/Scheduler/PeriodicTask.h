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

    if (now - lastRunAt_ < intervalMs_)
      return;

    lastRunAt_ = now;
    callback_();
  }

private:
  Callback callback_;
  uint32_t intervalMs_;
  uint32_t lastRunAt_ = 0;
  bool started_ = false;
};

#endif
